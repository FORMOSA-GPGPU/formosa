#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

"""Validate complete Parboil outputs against upstream CPU implementations."""

import argparse
import math
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile


def run(command, cwd=None):
    result = subprocess.run(
        command, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
    )
    print(result.stdout, end="", flush=True)
    if result.returncode or re.search(
        r"\b(?:ERROR|Error|error)(?:\s*[:!]|\s*\()", result.stdout
    ):
        raise ValueError("benchmark reported an error")


def read_output(path, kind):
    if kind == "tpacf":
        values = list(map(int, path.read_text().split()))
        shape, count = (60,), 60
    elif kind == "sgemm":
        fields = path.read_text().split()
        shape = tuple(map(int, fields[:2]))
        if len(shape) != 2 or min(shape) <= 0:
            raise ValueError("invalid matrix dimensions")
        values = list(map(float, fields[2:]))
        count = shape[0] * shape[1]
    else:
        data = path.read_bytes()
        if kind == "histo":
            shape = struct.unpack_from("<2I", data)
            size = count = shape[0] * shape[1]
            values = list(data[8:])
        elif kind == "sad":
            size, reserved = struct.unpack_from("<2I", data)
            if reserved != 0:
                raise ValueError("invalid SAD header")
            shape, count = (size,), size * 41 * 1089
            values = list(struct.unpack(f"<{count}H", data[8:]))
        elif kind == "lbm":
            size = count = len(data) // 4
            shape = (size,)
            values = list(struct.unpack(f"<{count}f", data))
        elif kind == "cutcp":
            magnitude, size = struct.unpack_from("<fI", data)
            shape = (size,)
            values = [magnitude, *struct.unpack(f"<{2 * size}f", data[8:])]
            count = 1 + 2 * size
        else:
            size = struct.unpack_from("<I", data)[0]
            shape = (size,)
            count = size * {"mri-q": 2, "mri-gridding": 3}.get(kind, 1)
            values = list(struct.unpack(f"<{count}f", data[4:]))
        if size <= 0:
            raise ValueError("invalid output dimensions")
    if len(values) != count or not all(math.isfinite(v) for v in values):
        raise ValueError("incomplete or nonfinite output")
    return shape, values


def compare(kind, output, reference):
    shape, values = read_output(output, kind)
    reference_shape, expected = read_output(reference, kind)
    if shape != reference_shape or len(values) != len(expected):
        raise ValueError("output dimensions differ from CPU reference")
    if not any(abs(v) > 1e-6 for v in expected):
        raise ValueError("CPU reference is trivial; expected nonzero results")

    # Use the original numerical limits, checking every stencil value instead
    # of only a quarter. Enforce CUTCP's intended 0.5% point limit; the upstream
    # Python 2 comparator's ratio is inverted.
    relative = {
        "sgemm": 0.01,
        "stencil": 0.002,
        "mri-q": 0.002,
        "spmv": 0.002,
        "cutcp": 0.005,
        "histo": 0,
        "sad": 0,
        "tpacf": 0,
        "lbm": 0.002,
        "mri-gridding": 0.002,
    }[kind]
    absolute = {
        "sgemm": 0.01,
        "stencil": 0.001,
        "cutcp": 1e-4,
        "histo": 0,
        "sad": 0,
        "tpacf": 0,
    }.get(kind, 1e-4 * max(abs(v) for v in expected))
    for index, (actual, wanted) in enumerate(zip(values, expected)):
        tolerance = absolute
        ratio = relative
        if kind == "cutcp" and index == 0:
            ratio = 0.0025
        if kind == "mri-gridding" and index >= 2 * shape[0]:
            # Density counts must match exactly even when grid magnitudes are large.
            tolerance = ratio = 0
        if abs(actual - wanted) > max(tolerance, ratio * abs(wanted)):
            raise ValueError(
                f"output {index} differs from CPU reference: {actual} vs {wanted}"
            )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--kind",
        choices=[
            "sgemm",
            "stencil",
            "mri-q",
            "spmv",
            "cutcp",
            "histo",
            "lbm",
            "mri-gridding",
            "sad",
            "tpacf",
        ],
        required=True,
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--reference", required=True)
    parser.add_argument("--reference-arg", action="append", default=[])
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    try:
        # A previous run's output must never make an incomplete run pass.
        args.output.unlink(missing_ok=True)
        run(command)
        with tempfile.TemporaryDirectory(
            prefix="formosa-parboil-reference-"
        ) as directory:
            reference = Path(directory) / "result.dat"
            run(
                [args.reference, "-o", str(reference), *args.reference_arg],
                cwd=directory,
            )
            compare(args.kind, args.output, reference)
        print(f"Parboil {args.kind}: complete output matches upstream CPU reference")
    except (OSError, ValueError, struct.error) as error:
        print(f"Parboil validation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
