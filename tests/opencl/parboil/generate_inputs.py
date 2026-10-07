#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

"""Small deterministic inputs in the unchanged Parboil hosts' file formats."""

from pathlib import Path
import struct
import sys


def floats(values):
    return struct.pack(f"<{len(values)}f", *values)


def matrix(path, rows):
    height, width = len(rows), len(rows[0])
    values = [rows[i][j] for j in range(width) for i in range(height)]
    path.write_text(f"{height} {width}\n" + " ".join(str(v) for v in values) + "\n")


def generate(output):
    output.mkdir(parents=True, exist_ok=True)
    a = [[((3 * i + 5 * j) % 17 + 1) / 8 for j in range(16)] for i in range(16)]
    b = [[((7 * i + 11 * j) % 19 + 1) / 8 for j in range(32)] for i in range(16)]
    matrix(output / "sgemm_a.txt", a)
    matrix(output / "sgemm_b.txt", b)
    matrix(output / "sgemm_bt.txt", list(zip(*b)))

    stencil = [
        ((3 * x + 5 * y + 7 * z) % 23 + 1) / 16
        for z in range(4)
        for y in range(4)
        for x in range(16)
    ]
    (output / "stencil.bin").write_bytes(floats(stencil))

    # ComputeQ processes entire 256-pixel tiles without a tail bounds check.
    num_k, num_x = 8, 256
    trajectory = [
        [((i * stride) % 17 - 8) / 32 for i in range(num_k)] for stride in (3, 5, 7)
    ]
    positions = [
        [((i * stride) % 31 - 15) / 32 for i in range(num_x)] for stride in (3, 5, 7)
    ]
    phi = [
        [(i % 7 + 1) / 8 for i in range(num_k)],
        [(i % 5 + 1) / 16 for i in range(num_k)],
    ]
    (output / "mri-q.bin").write_bytes(
        struct.pack("<ii", num_k, num_x)
        + b"".join(floats(v) for v in [*trajectory, *positions, *phi])
    )

    size = 32
    entries = [(i, i, 2 + i / 32) for i in range(size)]
    entries += [(i, i + 1, (i % 5 + 1) / 8) for i in range(size - 1)]
    entries += [(i, i + 3, 1 / 16) for i in range(0, size - 3, 3)]
    (output / "spmv.mtx").write_text(
        "%%MatrixMarket matrix coordinate real symmetric\n"
        + f"{size} {size} {len(entries)}\n"
        + "".join(f"{i + 1} {j + 1} {value}\n" for i, j, value in entries)
    )
    (output / "spmv-vector.bin").write_bytes(
        floats([(i % 11 + 1) / 16 for i in range(size)])
    )

    atoms = [(0, 0, 0, 1), (1.5, 0, 0, -0.5), (0, 1.5, 0, 0.75), (0, 0, 1.5, -0.25)]
    (output / "cutcp.pqr").write_text(
        "".join(
            f"ATOM  {i} C MOL 1 {x:.3f} {y:.3f} {z:.3f} {charge:.3f}\n"
            for i, (x, y, z, charge) in enumerate(atoms, 1)
        )
    )


if __name__ == "__main__":
    generate(Path(sys.argv[1]))
