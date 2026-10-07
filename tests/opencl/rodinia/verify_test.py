#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

"""Exercise the built LUD comparator and wrapper with deliberately bad results."""

from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    compiler, common = sys.argv[1], Path(sys.argv[2])
    with tempfile.TemporaryDirectory(prefix="rodinia-validation-") as directory:
        source = Path(directory, "lud.c")
        program = Path(directory, "lud")
        source.write_text(
            '#include <stdio.h>\n#include <stdlib.h>\n#include "common.h"\n'
            "int main(int argc, char **argv) {\n"
            "  float expected = 1, actual = strtof(argv[1], NULL);\n"
            '  puts(">>>Verify<<<<");\n'
            "  lud_verify(&expected, &actual, 1);\n"
            "  return 0;\n}\n"
        )
        subprocess.run(
            [
                compiler,
                "-std=gnu99",
                "-I",
                str(common),
                str(source),
                str(common / "common.c"),
                "-lm",
                "-o",
                str(program),
            ],
            check=True,
        )
        for value in ["1", "2", "nan", "inf", "-inf"]:
            result = subprocess.run(
                [
                    sys.executable,
                    str(Path(__file__).with_name("verify.py")),
                    "--kind",
                    "lud",
                    "--",
                    str(program),
                    value,
                ],
                capture_output=True,
                text=True,
            )
            output = result.stdout + result.stderr
            if value == "1":
                assert result.returncode == 0, output
            else:
                assert result.returncode == 1, output
                assert "benchmark reported an error or mismatch" in output, output
    print("LUD validation rejects incorrect finite values, NaN, and infinities")


if __name__ == "__main__":
    main()
