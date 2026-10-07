#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

"""Deterministic, nonuniform inputs in the formats accepted by upstream hosts."""

from pathlib import Path
import sys


def generate(output):
    output.mkdir(parents=True, exist_ok=True)
    for width, layers, suffix in [(16, 1, "16"), (64, 2, "64x64x2")]:
        temperature, power = [], []
        for z in range(layers):
            for y in range(width):
                for x in range(width):
                    temperature.append(60 + ((3 * x + 5 * y + 7 * z) % 37) * 0.5)
                    power.append(1e6 + ((11 * x + 13 * y + 17 * z) % 29) * 1e4)
        for name, values in [("temperature", temperature), ("power", power)]:
            (output / f"{name}{suffix}.txt").write_text(
                "".join(f"{value:.6f}\n" for value in values)
            )
    size = 32
    # Strict diagonal dominance avoids the upstream random generator's dependence
    # on wall-clock time and keeps LU verification finite and well-conditioned.
    matrix = [
        [4 + i / 16 if i == j else ((i + j) % 7 - 3) / 32 for j in range(size)]
        for i in range(size)
    ]
    (output / "matrix32.txt").write_text(
        f"{size}\n"
        + "\n".join(" ".join(f"{v:.6f}" for v in row) for row in matrix)
        + "\n"
    )
    # Enough keys to split the original order-256 tree into multiple leaves.
    (output / "btree.txt").write_text("256\n" + "".join(f"{i}\n" for i in range(256)))
    # Space delimiters also keep the CPU host's legacy parser from scanning
    # past a newline. Exercise both queries without changing either parser.
    (output / "btree-commands.txt").write_text("k8 \nj4 4 \n")
    # SRAD always reads a 502x458 PGM and crops/tiles it to the requested size.
    pixels = [50 + (3 * x + 5 * y) % 150 for y in range(502) for x in range(458)]
    (output / "srad.pgm").write_text(
        "P2\n458 502\n255\n" + " ".join(map(str, pixels)) + "\n"
    )
    (output / "kmeans100.txt").write_text(
        "".join(
            str(point)
            + " "
            + " ".join(str((point * 100 + feature) % 64) for feature in range(100))
            + "\n"
            for point in range(100)
        )
    )
    (output / "input.rgb").write_bytes(
        bytes((index * 7 + index // 3) % 256 for index in range(32 * 32 * 3))
    )
    data = Path(__file__).resolve().parent / "data/myocyte"
    params = (data / "params.txt").read_text().split()
    if len(params) != 16 or len((data / "y.txt").read_text().split()) != 91:
        raise ValueError("incomplete original Myocyte physiological data")
    myocyte = output / "myocyte"
    myocyte.mkdir(exist_ok=True)
    (myocyte / "y.txt").write_bytes((data / "y.txt").read_bytes())
    # The OpenCL model places the pacing period after its three five-value
    # CAM groups, then reads K and Mg explicitly. The CPU model stores the
    # period first and fixes K=135 and Mg=1 in cam.c. Supply a complete input
    # in the OpenCL format rather than letting its reader run beyond EOF.
    (myocyte / "params.txt").write_text(
        "\n".join([*params[1:], params[0], "135", "1"]) + "\n"
    )


if __name__ == "__main__":
    generate(Path(sys.argv[1]))
