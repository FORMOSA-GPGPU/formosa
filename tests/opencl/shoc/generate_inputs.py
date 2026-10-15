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

# A 1100-vertex frontier spills the local queue and enters the multi-block kernel.
# Converging paths, a third level, and an unreachable component check path lengths.
adjacency = [set() for _ in range(1536)]
for vertex in range(1, 1528):
    if vertex <= 1100:
        neighbors = {0}
    elif vertex <= 1312:
        neighbors = {1 + (vertex - 1101) % 1100, 1 + (vertex - 1094) % 1100}
    else:
        neighbors = {1101 + (vertex - 1313) % 212, 1101 + (vertex - 1306) % 212}
    for neighbor in neighbors:
        adjacency[vertex].add(neighbor)
        adjacency[neighbor].add(vertex)
for vertex in range(1528, 1535):
    adjacency[vertex].add(vertex + 1)
    adjacency[vertex + 1].add(vertex)
(output / "graph.metis").write_text(
    f"1536 {sum(map(len, adjacency)) // 2}\n"
    + "".join(" ".join(str(v + 1) for v in sorted(row)) + "\n" for row in adjacency)
)
