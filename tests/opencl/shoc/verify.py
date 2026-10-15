#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

"""Require complete CPU/GPU checks; SHOC hosts can exit zero on failure."""

import argparse
import json
import math
from pathlib import Path
import re
import subprocess
import sys

from verify_level0 import KINDS as LEVEL0_KINDS, validate as validate_level0


def validate(kind, output):
    if re.search(r"\b(?:error|failed|failure|exception|mismatch)\b", output, re.I):
        # Stencil's successful checks and explanatory text contain "errors",
        # plural, so singular error tokens remain failures.
        raise ValueError("SHOC reported an error or failed comparison")
    if kind in LEVEL0_KINDS:
        validate_level0(kind, output)
    elif kind == "spmv":
        if (
            len(re.findall(r"^Test Passed\s*$", output, re.M)) != 10
            or "Single precision tests" not in output
            or "Double precision tests" not in output
        ):
            raise ValueError("SpMV did not complete all ten CPU/GPU comparisons")
    elif kind == "stencil2d":
        counts = re.findall(r"^pass \d+: (\d+) validation errors\s*$", output, re.M)
        if counts != ["0", "0"]:
            raise ValueError("Stencil2D did not pass both precision comparisons")
    elif kind in {
        "bfs",
        "bfs_uiuc",
        "fft",
        "gemm",
        "md",
        "reduction",
        "scan",
        "sort",
        "triad",
    }:
        expected = {
            "bfs": 1,
            "bfs_uiuc": 1,
            "fft": 6,
            "gemm": 4,
            "md": 2,
            "reduction": 2,
            "scan": 2,
            "sort": 1,
            "triad": 1,
        }[kind]
        if len(re.findall(r"^Test Passed\s*$", output, re.M)) != expected:
            raise ValueError(
                f"{kind} did not complete all {expected} numerical comparisons"
            )
    elif kind == "md5hash":
        if output.count("Successfully found match (index, key, hash):") != 1:
            raise ValueError("MD5 did not validate the recovered index, key and digest")
    elif kind == "s3d":
        reference = json.loads(
            (Path(__file__).parent / "references/s3d.json").read_text()
        )["species"]
        rows = re.findall(r"^S3D (4|8) (\d+) (\d+) (\S+)$", output, re.M)
        expected_keys = {
            (precision, species, point)
            for precision in ("4", "8")
            for species in range(22)
            for point in range(512)
        }
        seen = set()
        for precision, species, point, raw in rows:
            key = precision, int(species), int(point)
            if key not in expected_keys or key in seen:
                raise ValueError(f"S3D duplicate or unexpected output {key}")
            seen.add(key)
            value = float(raw)
            expected = reference[precision][int(species)]
            if not math.isfinite(value) or not math.isclose(
                value,
                expected,
                rel_tol=1e-3 if precision == "4" else 1e-8,
                abs_tol=1e-30,
            ):
                raise ValueError(
                    f"S3D mismatch at {key}: expected {expected}, got {value}"
                )
        if seen != expected_keys:
            raise ValueError(
                "S3D did not return every grid point and species in both precisions"
            )
    else:
        raise ValueError(f"Unknown SHOC benchmark {kind}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--kind",
        choices=sorted(
            LEVEL0_KINDS
            | {
                "spmv",
                "stencil2d",
                "bfs",
                "bfs_uiuc",
                "fft",
                "gemm",
                "md",
                "md5hash",
                "reduction",
                "scan",
                "sort",
                "triad",
                "s3d",
            }
        ),
        required=True,
    )
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    result = subprocess.run(
        command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
    )
    print(
        "\n".join(
            line for line in result.stdout.splitlines() if not line.startswith("S3D ")
        ),
        flush=True,
    )
    if result.returncode:
        raise ValueError(f"command exited with status {result.returncode}")
    validate(args.kind, result.stdout)
    print(f"SHOC {args.kind}: validation passed", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        print(f"SHOC validation failed: {error}", file=sys.stderr)
        sys.exit(1)
