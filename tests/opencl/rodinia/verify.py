#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

"""Validate original Rodinia computations, including hosts that return zero on errors."""

import argparse
import base64
import json
import math
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

import reference as cpu


def run(command, cwd=None):
    result = subprocess.run(
        command, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
    )
    print(result.stdout, end="", flush=True)
    if result.returncode:
        raise ValueError(f"command exited with status {result.returncode}")
    if re.search(
        r"\b(?:ERROR|Error|error)\s*[:!]|\bdismatch\b|\bfailed\b", result.stdout, re.I
    ):
        raise ValueError("benchmark reported an error or mismatch")
    return result.stdout


def verify_readbacks(
    kind, directory, arguments, output, output_file=None, reference=None
):
    files = sorted(directory.glob("readback-*.bin"))
    if not files:
        raise ValueError("benchmark produced no device readbacks")
    if kind == "bfs":
        cpu.compare(cpu.unpack(files[-1], "i"), cpu.bfs(Path(arguments[0])))
        if "--cambine:passed:-)" not in output:
            raise ValueError("original BFS CPU check did not complete")
    elif kind == "gaussian":
        expected = cpu.gaussian(Path(arguments[0]))
        if len(files) != 3:
            raise ValueError("missing Gaussian matrix/vector readbacks")
        for file, values in zip(files, expected):
            cpu.compare(cpu.unpack(file), values, 2e-5, 1e-5)
    elif kind == "kmeans":
        expected, centers = cpu.kmeans(Path(arguments[0]))
        if len(files) != len(expected):
            raise ValueError("missing Kmeans iteration readbacks")
        for file, members in zip(files, expected):
            cpu.compare(cpu.unpack(file, "i"), members)
        actual_centers = [
            [float(v) for v in values.split()]
            for _, values in re.findall(r"^([0-4]):([^\n]+)$", output, re.M)
        ]
        if len(actual_centers) != 5:
            raise ValueError("missing Kmeans centroid output")
        for actual, expected in zip(actual_centers, centers):
            cpu.compare(actual, expected, 0.0051)
    elif kind == "nn":
        if len(files) != 1:
            raise ValueError("missing NN distance readback")
        cpu.compare(cpu.unpack(files[0]), cpu.nn(Path(arguments[0])), 5e-5, 1e-6)
    elif kind == "backprop":
        if len(files) != 3 or not reference:
            raise ValueError("missing Backprop reference or training readbacks")
        reference_file = directory / "cpu-partials.bin"
        run([reference, str(reference_file)])
        cpu.compare(cpu.unpack(files[0]), cpu.unpack(reference_file), 1e-5, 1e-5)
    elif kind == "lavamd":
        uploads = sorted(directory.glob("upload-*.bin"))
        if len(files) != 1 or len(uploads) != 4:
            raise ValueError("missing LavaMD input/output buffer transfers")
        # One box with zero neighbors, followed by positions, charges and
        # initially zero forces. Use the host's actual time-seeded inputs.
        box = uploads[0].read_bytes()
        if len(box) != 656 or any(box[:28]):
            raise ValueError("LavaMD input does not describe one isolated box")
        expected = cpu.lavamd(cpu.unpack(uploads[1]), cpu.unpack(uploads[2]))
        cpu.compare(cpu.unpack(uploads[3]), [0.0] * 400)
        actual = cpu.unpack(files[0])
        cpu.compare(actual, expected, 5e-5, 1e-5)
        text_values = [
            float(value)
            for line in output_file.read_text().splitlines()
            for value in line.split(",")
        ]
        cpu.compare(text_values, actual, 5.1e-7)
        error = max(abs(a - b) for a, b in zip(actual, expected))
        print(f"LavaMD: checked 400 values; maximum absolute error {error:.6g}")
    else:
        golden = json.loads(
            (Path(__file__).parent / "references" / f"{kind}.json").read_text()
        )
        if len(files) != len(golden["arrays"]):
            raise ValueError("missing CPU reference readbacks")
        for file, expected in zip(files, golden["arrays"]):
            cpu.compare(
                cpu.unpack(file, expected["type"]),
                expected["values"],
                expected["absolute_tolerance"],
                expected["relative_tolerance"],
            )
        if kind == "dwt2d":
            for suffix, expected in golden["outputs"].items():
                if Path(str(output_file) + suffix).read_bytes() != base64.b64decode(
                    expected
                ):
                    raise ValueError(f"DWT output {suffix} differs from CPU reference")
        elif kind == "particlefilter":
            for file in files[2::3]:
                weights = cpu.unpack(file)
                if any(v < 0 for v in weights) or abs(sum(weights) - 1) > 1e-5:
                    raise ValueError("Particlefilter weights are not normalized")
            values = dict(
                re.findall(r"^(XE|YE|distance):\s*(\S+)", output_file.read_text(), re.M)
            )
            if set(values) != {"XE", "YE", "distance"}:
                raise ValueError("missing Particlefilter position output")
            x, y, weights = [cpu.unpack(file) for file in files[-3:]]
            xe = sum(a * w for a, w in zip(x, weights))
            ye = sum(a * w for a, w in zip(y, weights))
            cpu.compare(
                [float(values[k]) for k in ["XE", "YE", "distance"]],
                [xe, ye, math.hypot(xe - 8, ye - 8)],
                3e-5,
            )


def integer_rows(output):
    return [
        [int(value) for value in line.split()]
        for line in output.splitlines()
        if re.fullmatch(r"\s*-?\d+(?:\s+-?\d+)+\s*", line)
    ]


def read_temperatures(path):
    rows = [line.split() for line in path.read_text().splitlines() if line.strip()]
    if not rows or any(len(row) != 2 for row in rows):
        raise ValueError("missing or malformed temperature output")
    indices = [int(row[0]) for row in rows]
    if indices != list(range(len(rows))):
        raise ValueError("temperature output indices are incomplete")
    values = [float(row[1]) for row in rows]
    if not all(math.isfinite(value) for value in values):
        raise ValueError("temperature output contains nonfinite values")
    return values


def read_pgm(path):
    tokens = re.sub(r"#[^\n]*", "", path.read_text()).split()
    if tokens[:4] != ["P2", "16", "16", "255"]:
        raise ValueError("SRAD output has incorrect PGM dimensions or format")
    values = [int(value) for value in tokens[4:]]
    if len(values) != 256 or any(value < 0 or value > 255 for value in values):
        raise ValueError("SRAD output pixels are incomplete or invalid")
    return values


def read_centers(path):
    rows = [line.split() for line in path.read_text().splitlines() if line.strip()]
    if len(rows) not in (6, 9) or [len(row) for row in rows] != [1, 1, 2] * (
        len(rows) // 3
    ):
        raise ValueError("Streamcluster center output is incomplete")
    centers = {}
    for index in range(0, len(rows), 3):
        identifier = int(rows[index][0])
        weight = float(rows[index + 1][0])
        coordinates = list(map(float, rows[index + 2]))
        if (
            identifier in centers
            or not 0 <= identifier < 256
            or not math.isfinite(weight)
            or weight <= 0
            or any(not math.isfinite(v) or not 0 <= v <= 1 for v in coordinates)
        ):
            raise ValueError("Streamcluster center values are invalid")
        centers[identifier] = [weight, *coordinates]
    if sum(values[0] for values in centers.values()) != 256:
        raise ValueError("Streamcluster centers do not account for all points")
    return centers


def hotspot_reference(arguments):
    rows, cols, iterations = map(int, arguments[:3])
    temperature = [float(v) for v in Path(arguments[4]).read_text().split()]
    power = [float(v) for v in Path(arguments[5]).read_text().split()]
    if len(temperature) != rows * cols or len(power) != rows * cols:
        raise ValueError("Hotspot input dimensions are incomplete")
    # Independent forward-Euler stencil with the OpenCL host's physical
    # constants and time step. The upstream OpenMP host divides its step by
    # an additional 1000, so it cannot serve as an oracle for this host.
    height, width = 0.016 / rows, 0.016 / cols
    capacity = 0.5 * 1.75e6 * 0.0005 * width * height
    rx = width / (2 * 100 * 0.0005 * height)
    ry = height / (2 * 100 * 0.0005 * width)
    rz = 0.0005 / (100 * height * width)
    step = 0.001 / (3e6 / (0.5 * 0.0005 * 1.75e6))
    for _ in range(iterations):
        result = []
        for row in range(rows):
            for col in range(cols):
                index = row * cols + col
                center = temperature[index]
                north = temperature[max(row - 1, 0) * cols + col]
                south = temperature[min(row + 1, rows - 1) * cols + col]
                west = temperature[row * cols + max(col - 1, 0)]
                east = temperature[row * cols + min(col + 1, cols - 1)]
                result.append(
                    center
                    + step
                    / capacity
                    * (
                        power[index]
                        + (north + south - 2 * center) / ry
                        + (west + east - 2 * center) / rx
                        + (80 - center) / rz
                    )
                )
        temperature = result
    return temperature


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--kind",
        required=True,
        choices=[
            "hotspot",
            "hotspot3d",
            "lud",
            "nw",
            "pathfinder",
            "btree",
            "srad",
            "bfs",
            "gaussian",
            "kmeans",
            "nn",
            "backprop",
            "lavamd",
            "particlefilter",
            "dwt2d",
            "streamcluster",
        ],
    )
    parser.add_argument("--output", type=Path)
    parser.add_argument("--reference")
    parser.add_argument("--readback-library", type=Path)
    parser.add_argument("--reference-arg", action="append", default=[])
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if args.output:
        args.output.unlink(missing_ok=True)
        if args.kind == "dwt2d":
            for suffix in [".r", ".g", ".b"]:
                Path(str(args.output) + suffix).unlink(missing_ok=True)
    if args.readback_library:
        with tempfile.TemporaryDirectory(prefix="formosa-readbacks-") as directory:
            recorded_command = list(command)
            index = recorded_command.index("--") + 1
            recorded_command[index:index] = [
                "/usr/bin/env",
                f"LD_PRELOAD={args.readback_library}",
                f"FORMOSA_BENCHMARK_READBACK={directory}",
            ]
            if args.kind == "lavamd":
                recorded_command.insert(
                    index + 3, f"FORMOSA_BENCHMARK_UPLOAD={directory}"
                )
            output = run(recorded_command)
            verify_readbacks(
                args.kind,
                Path(directory),
                args.reference_arg,
                output,
                args.output,
                args.reference,
            )
        print(f"Rodinia {args.kind}: validation passed", flush=True)
        return 0
    output = run(command)
    if args.kind == "lud":
        if ">>>Verify<<<<" not in output:
            raise ValueError("LU verification did not complete")
    elif args.kind == "hotspot3d":
        matches = re.findall(r"Accuracy:\s*(\S+)", output)
        if (
            len(matches) != 1
            or not math.isfinite(float(matches[0]))
            or float(matches[0]) > 1e-3
        ):
            raise ValueError("Hotspot3D RMS error exceeds 0.001")
        if len(read_temperatures(args.output)) != 64 * 64 * 2:
            raise ValueError("Hotspot3D output is incomplete")
    elif args.kind == "hotspot":
        values = read_temperatures(args.output)
        reference = hotspot_reference(args.reference_arg)
        if len(values) != len(reference):
            raise ValueError("Hotspot output is incomplete")
        if any(abs(a - b) > 1e-3 for a, b in zip(values, reference)):
            raise ValueError("Hotspot temperatures differ from the CPU reference")
    else:
        # References write the same filenames as GPU hosts; isolate them from
        # each other and preserve the GPU output for inspection.
        with tempfile.TemporaryDirectory(prefix="formosa-reference-") as directory:
            reference_directory = Path(directory)
            reference_arguments = args.reference_arg
            if args.kind == "srad":
                # The CPU program reads ../../../data/srad/image.pgm. Stage its
                # original path in a temporary tree instead of changing it.
                image_directory = Path(directory, "data", "srad")
                image_directory.mkdir(parents=True)
                shutil.copyfile(reference_arguments[-1], image_directory / "image.pgm")
                reference_directory = Path(directory, "work", "reference", "srad")
                reference_directory.mkdir(parents=True)
                reference_arguments = reference_arguments[:-1]
            reference_output = run(
                [args.reference, *reference_arguments], cwd=reference_directory
            )
            if args.kind == "pathfinder":
                rows, reference_rows = integer_rows(output), integer_rows(
                    reference_output
                )
                if (
                    len(rows) != 10
                    or len(reference_rows) != 10
                    or rows != reference_rows
                ):
                    raise ValueError("Pathfinder rows differ from the CPU reference")
            elif args.kind == "nw":
                reference = Path(directory, args.output.name).read_text()
                if args.output.read_text() != reference:
                    raise ValueError(
                        "Needleman-Wunsch traceback differs from the CPU reference"
                    )
            elif args.kind == "btree":
                reference = Path(directory, "output.txt").read_text()
                actual = args.output.read_text()
                rows = integer_rows(actual)
                if (
                    len(rows) != 12
                    or [len(row) for row in rows] != [2] * 8 + [3] * 4
                    or actual != reference
                ):
                    raise ValueError(
                        "B+tree point/range queries differ from the CPU reference"
                    )
            elif args.kind == "srad":
                actual = read_pgm(args.output)
                expected = read_pgm(reference_directory / "image_out.pgm")
                # Both programs truncate floats to integer PGM pixels. Allow a
                # one-level difference at an integer boundary after diffusion.
                if any(abs(a - b) > 1 for a, b in zip(actual, expected)):
                    raise ValueError("SRAD pixels differ from the CPU reference")
            elif args.kind == "streamcluster":
                actual = read_centers(args.output)
                expected = read_centers(reference_directory / "centers.txt")
                if actual.keys() != expected.keys():
                    raise ValueError(
                        "Streamcluster center IDs differ from CPU reference"
                    )
                for identifier in expected:
                    cpu.compare(actual[identifier], expected[identifier], 2e-6)
    print(f"Rodinia {args.kind}: validation passed", flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as error:
        print(f"Rodinia validation failed: {error}", file=sys.stderr)
        sys.exit(1)
