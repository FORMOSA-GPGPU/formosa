#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

"""Exercise the built SHOC comparators and wrapper with deliberately bad results."""

from pathlib import Path
import re
import subprocess
import sys
import tempfile


def main():
    compiler = sys.argv[1]
    source_root, config = map(Path, sys.argv[2:])
    common = source_root / "common"
    spmv = (source_root / "opencl/level1/spmv/Spmv.cpp").read_text()
    start = spmv.index("template <typename floatType>\nbool verifyResults")
    end = spmv.index("\n}\n", start) + 3
    tolerance = re.search(
        r"^static const double MAX_RELATIVE_ERROR = .+;",
        (common / "Spmv/util.h").read_text(),
        re.M,
    ).group()
    programs = {
        "spmv": (
            "#include <iostream>\n#include <math.h>\n#include <stdlib.h>\n"
            "using namespace std;\n"
            + tolerance
            + "\n"
            + spmv[start:end]
            + "\nint main(int argc, char **argv) {\n"
            "  bool single = argv[3][0] == 'f';\n"
            "  float cpu = single ? strtof(argv[1], NULL) : 1;\n"
            "  float gpu = single ? strtof(argv[2], NULL) : 1;\n"
            '  cout << "Single precision tests" << endl;\n'
            "  for (int i = 0; i < 5; ++i) verifyResults(&cpu, &gpu, 1, i);\n"
            "  double dcpu = single ? 1 : strtod(argv[1], NULL);\n"
            "  double dgpu = single ? 1 : strtod(argv[2], NULL);\n"
            '  cout << "Double precision tests" << endl;\n'
            "  for (int i = 0; i < 5; ++i) verifyResults(&dcpu, &dgpu, 1, i);\n}\n"
        ),
        "stencil2d": (
            "#include <iostream>\n#include <math.h>\n#include <stdlib.h>\n"
            '#include "ValidateMatrix2D.cpp"\n#include "Matrix2DStatics.cpp"\n'
            "template<class T> void trial(int pass, T expected, T actual) {\n"
            "  Matrix2D<T> cpu(1, 1), gpu(1, 1);\n"
            "  cpu.GetData()[0][0] = expected; gpu.GetData()[0][0] = actual;\n"
            "  Validate<T> check(.01);\n"
            '  std::cout << "pass " << pass << ": " << check(cpu, gpu).size()\n'
            '            << " validation errors" << std::endl;\n}\n'
            "int main(int argc, char **argv) {\n"
            "  bool single = argv[3][0] == 'f';\n"
            "  trial<float>(0, single ? strtof(argv[1], NULL) : 1,\n"
            "                  single ? strtof(argv[2], NULL) : 1);\n"
            "  trial<double>(1, single ? 1 : strtod(argv[1], NULL),\n"
            "                   single ? 1 : strtod(argv[2], NULL));\n}\n"
        ),
    }
    cases = [
        ("1", "1", True),
        ("-1", "-1", True),
        ("0", "0", True),
        ("-0", "0", True),
        ("1", "1.005", True),
        ("-1", "-1.005", True),
        ("1", "1.05", False),
        ("-1", "-1.05", False),
        ("-1", "100", False),
        ("0", "100", False),
        ("1e-8", "0", False),
        ("-1e-8", "0", False),
        ("0", "5e-7", True),
        ("0", "-5e-7", True),
        ("0", "1e-6", True),
        ("0", "-1e-6", True),
        ("0", "2e-6", False),
        ("0", "-2e-6", False),
        ("1", "2", False),
    ]
    for value in ["nan", "inf", "-inf"]:
        cases.extend([("1", value, False), (value, "1", False), (value, value, False)])
    with tempfile.TemporaryDirectory(prefix="shoc-validation-") as directory:
        for kind, code in programs.items():
            source, program = Path(directory, kind + ".cpp"), Path(directory, kind)
            source.write_text(code)
            subprocess.run(
                [
                    compiler,
                    "-std=c++11",
                    "-I",
                    str(common),
                    "-I",
                    str(config),
                    str(source),
                    "-o",
                    str(program),
                ],
                check=True,
            )
            for precision in ["float", "double"]:
                precision_cases = cases[:]
                precision_cases.extend(
                    [
                        ("1", "1.015", kind == "spmv"),
                        ("-1", "-1.015", kind == "spmv"),
                    ]
                )
                if precision == "double":
                    # Catch accidental narrowing of a double error to float.
                    precision_cases.extend(
                        [("1e-50", "1.001e-50", True), ("1e-50", "2e-50", False)]
                    )
                for expected, actual, passed in precision_cases:
                    result = subprocess.run(
                        [
                            sys.executable,
                            str(Path(__file__).with_name("verify.py")),
                            "--kind",
                            kind,
                            "--",
                            str(program),
                            expected,
                            actual,
                            precision,
                        ],
                        capture_output=True,
                        text=True,
                    )
                    output = result.stdout + result.stderr
                    context = (
                        f"{kind} {precision}: expected={expected}, actual={actual}\n"
                    )
                    assert result.returncode == (0 if passed else 1), context + output
                    if not passed:
                        reason = (
                            "SHOC reported an error or failed comparison"
                            if kind == "spmv"
                            else "Stencil2D did not pass both precision comparisons"
                        )
                        assert reason in output, context + output
    print(
        "SHOC float/double validation covers signed/zero references and nonfinite values"
    )


if __name__ == "__main__":
    main()
