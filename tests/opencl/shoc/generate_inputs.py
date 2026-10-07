#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

"""A small nonuniform matrix for SHOC SpMV."""

from pathlib import Path
import sys


output = Path(sys.argv[1])
output.mkdir(parents=True, exist_ok=True)
size = 256
entries = []
for row in range(size):
    entries.extend(
        [
            (row + 1, row + 1, 4),
            (row + 1, (row + 1) % size + 1, -0.5),
            (row + 1, (row + 17) % size + 1, 0.25),
        ]
    )
(output / "matrix.mtx").write_text(
    f"%%MatrixMarket matrix coordinate real general\n{size} {size} {len(entries)}\n"
    + "".join(f"{row} {col} {value}\n" for row, col, value in entries)
)
