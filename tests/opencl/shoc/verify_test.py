#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

"""Exercise the built SHOC comparators and wrapper with deliberately bad results."""

from pathlib import Path
import re
import json

from verify import validate
import subprocess
import sys
import tempfile


def verify_new_comparators(compiler, source_root, directory):
    level1 = source_root / "opencl/level1"

    def function(path, signature):
        source = (level1 / path).read_text()
        start = source.index(signature)
        return source[start : source.index("\n}\n", start) + 3]

    code = (
        "#include <algorithm>\n#include <vector>\n#include <iostream>\n"
        "#include <math.h>\n#include <stdlib.h>\n#include <string>\n"
        '#include "MD.h"\nusing namespace std;\n'
        '#include "OptionParser.h"\n'
        "struct cplxflt {float x,y;};\nstruct cplxdbl {double x,y;};\n"
        + function(
            "md/MD.cpp",
            "template <class T, class forceVecType, class posVecType>\nbool checkResults",
        )
        + function("sort/Sort.cpp", "bool verifySort(")
        + function("sort/Sort.cpp", "void\naddBenchmarkSpecOptions(")
        + function("sort/Sort.cpp", "static void initializeSortInput(")
        + function("gemm/GEMM.cpp", "template <class T>\nvoid verifyGemm(")
        + function("fft/FFT.cpp", "template <class T2>\nvoid verifyTransform(")
        + r"""
int main(int argc, char **argv) {
    string kind = argv[1];
    int fault = atoi(argv[2]);
    if (kind == "sort-input") {
        OptionParser op;
        addBenchmarkSpecOptions(op);
        if (fault && !op.parse(vector<string>{"--validation-input"})) return 1;
        unsigned input[1024];
        initializeSortInput(input, 1024, op);
        if (!fault) {
            for (unsigned i = 0; i < 1024; ++i)
                if (input[i] != i % 16) return 1;
        } else {
            if (*max_element(input, input + 1024) <= 0x0fffffffu) return 1;
            sort(input, input + 1024);
            if (unique(input, input + 1024) - input < 900) return 1;
        }
        return 0;
    }
    if (kind == "sort") {
        unsigned input[] = {3, 0, 2, 1}, actual[] = {0, 1, 2, 3};
        if (fault) fill(actual, actual + 4, 0);
        return verifySort(actual, input, 4) ? 0 : 1;
    }
    if (kind == "md") {
        float4 positions[] = {{0,0,0,0}, {1,0,0,0}};
        float4 forces[] = {{64,0,0,0}, {-64,0,0,0}};
        int neighbors[256];
        for (int i=0; i<128; ++i) {neighbors[2*i]=1; neighbors[2*i+1]=0;}
        if (fault == 1) forces[0].x = 0;
        if (fault == 2) forces[0].x = NAN;
        if (fault == 3) forces[0].y = INFINITY;
        return checkResults<float,float4,float4>(forces, positions, neighbors, 2) ? 0 : 1;
    }
    if (kind == "gemm") {
        float a[] = {1, 2, 3, 4}, b[] = {5, 6, 7, 8};
        float actual[] = {23,34,31,46};
        if (fault == 1) fill(actual, actual + 4, 0);
        if (fault == 2) actual[0] = NAN;
        if (fault == 3) actual[0] = INFINITY;
        verifyGemm(a, b, actual, vector<float>(4, 0), 2, false);
    }
    if (kind == "fft") {
        cplxflt input[512] = {}, actual[512];
        input[0].x = 1;
        for (int i=0; i<512; ++i) actual[i] = {1,0};
        if (fault == 1) copy(input, input + 512, actual);
        if (fault == 2) actual[0].x = NAN;
        if (fault == 3) actual[0].y = INFINITY;
        verifyTransform(input, actual, 512, false);
    }
}
"""
    )
    source, program = Path(directory, "numerics.cpp"), Path(directory, "numerics")
    source.write_text(code)
    subprocess.run(
        [
            compiler,
            "-std=c++11",
            "-I",
            str(level1 / "md"),
            "-I",
            str(source_root / "common"),
            str(source),
            str(source_root / "common/OptionParser.cpp"),
            str(source_root / "common/Option.cpp"),
            "-o",
            str(program),
        ],
        check=True,
    )
    for validation_input in ("0", "1"):
        subprocess.run([str(program), "sort-input", validation_input], check=True)
    for kind in ("sort", "md", "gemm", "fft"):
        for fault in range(2 if kind == "sort" else 4):
            result = subprocess.run(
                [str(program), kind, str(fault)], capture_output=True, text=True
            )
            assert result.returncode == (1 if fault else 0), (
                kind,
                fault,
                result.stdout + result.stderr,
            )

    # The S3D wrapper must reject incomplete, duplicated and corrupt grid output.
    reference = json.loads((Path(__file__).parent / "references/s3d.json").read_text())[
        "species"
    ]
    output = "\n".join(
        f"S3D {precision} {species} {point} {value}"
        for precision, values in reference.items()
        for species, value in enumerate(values)
        for point in range(512)
    )
    validate("s3d", output)
    for corrupt in (
        "",
        output + "\n" + output.splitlines()[0],
        output.replace(output.splitlines()[0], "S3D 4 0 0 0", 1),
        output.replace(output.splitlines()[0], "S3D 4 0 0 nan", 1),
    ):
        try:
            validate("s3d", corrupt)
        except ValueError:
            pass
        else:
            raise AssertionError("S3D accepted corrupt output")


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
        verify_new_comparators(compiler, source_root, directory)
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
