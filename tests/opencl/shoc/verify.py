#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

"""Require complete CPU/GPU checks; SHOC hosts can exit zero on failure."""

import argparse
import re
import subprocess
import sys


def validate(kind, output):
    if re.search(r"\b(?:error|failed|failure|exception|mismatch)\b", output, re.I):
        # Stencil's successful checks and explanatory text contain "errors",
        # plural, so singular error tokens remain failures.
        raise ValueError("SHOC reported an error or failed comparison")
    if kind == "spmv":
        if (
            len(re.findall(r"^Test Passed\s*$", output, re.M)) != 10
            or "Single precision tests" not in output
            or "Double precision tests" not in output
        ):
            raise ValueError("SpMV did not complete all ten CPU/GPU comparisons")
    else:
        counts = re.findall(r"^pass \d+: (\d+) validation errors\s*$", output, re.M)
        if counts != ["0", "0"]:
            raise ValueError("Stencil2D did not pass both precision comparisons")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--kind", choices=["spmv", "stencil2d"], required=True)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    result = subprocess.run(
        command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
    )
    print(result.stdout, end="", flush=True)
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
