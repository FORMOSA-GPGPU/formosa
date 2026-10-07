# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

"""Independent CPU calculations for the original Rodinia host readbacks."""

from collections import deque
import math
from pathlib import Path
import struct


def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def unpack(path, code="f"):
    data = path.read_bytes()
    if len(data) % 4:
        raise ValueError("incomplete numeric readback")
    return struct.unpack("<" + code * (len(data) // 4), data)


def compare(actual, expected, absolute=0, relative=0):
    if len(actual) != len(expected):
        raise ValueError("numeric output has incorrect length")
    for index, (a, b) in enumerate(zip(actual, expected)):
        if not math.isfinite(a) or not math.isfinite(b):
            raise ValueError(f"nonfinite output at index {index}")
        if abs(a - b) > absolute + relative * abs(b):
            raise ValueError(f"numeric mismatch at index {index}: {a} != {b}")


def lavamd(positions, charges):
    """One-box particle forces for the original host's alpha = 0.5."""
    if len(positions) != 400 or len(charges) != 100:
        raise ValueError("LavaMD inputs must contain 100 particles")
    particles = [positions[index : index + 4] for index in range(0, 400, 4)]
    forces = []
    for potential, x, y, z in particles:
        result = [0.0] * 4
        for charge, (other_potential, ox, oy, oz) in zip(charges, particles):
            distance = potential + other_potential - (x * ox + y * oy + z * oz)
            interaction = math.exp(-0.5 * distance)
            result[0] += charge * interaction
            for axis, delta in enumerate((x - ox, y - oy, z - oz), 1):
                result[axis] += 2 * charge * interaction * delta
        forces.extend(result)
    return forces


def bfs(path):
    data = iter(map(int, path.read_text().split()))
    size = next(data)
    nodes = [(next(data), next(data)) for _ in range(size)]
    next(data)  # The original host explicitly selects source zero.
    edge_count = next(data)
    edges = [(next(data), next(data)) for _ in range(edge_count)]
    distance = [-1] * size
    distance[0] = 0
    queue = deque([0])
    while queue:
        node = queue.popleft()
        start, count = nodes[node]
        for target, _ in edges[start : start + count]:
            if distance[target] == -1:
                distance[target] = distance[node] + 1
                queue.append(target)
    return distance


def gaussian(path):
    tokens = path.read_text().split()
    size = int(tokens[0])
    a = [f32(float(v)) for v in tokens[1 : 1 + size * size]]
    b = [f32(float(v)) for v in tokens[1 + size * size : 1 + size * size + size]]
    multipliers = [0.0] * (size * size)
    for column in range(size - 1):
        for row in range(column + 1, size):
            factor = f32(a[row * size + column] / a[column * size + column])
            multipliers[row * size + column] = factor
            for j in range(column, size):
                a[row * size + j] = f32(
                    a[row * size + j] - factor * a[column * size + j]
                )
            b[row] = f32(b[row] - factor * b[column])
    return [a, b, multipliers]


def kmeans(path):
    features = [
        [f32(float(v)) for v in row.split()[1:]]
        for row in path.read_text().splitlines()
        if row.strip()
    ]
    count, dimensions = len(features), len(features[0])
    centers = [row[:] for row in features[:5]]
    membership = [-1] * count
    iterations = []
    for _ in range(501):
        updated = []
        for row in features:
            distances = []
            for center in centers:
                distance = 0.0
                for a, b in zip(row, center):
                    difference = f32(a - b)
                    distance = f32(distance + f32(difference * difference))
                distances.append(distance)
            updated.append(min(range(5), key=distances.__getitem__))
        iterations.append(updated)
        changed = sum(a != b for a, b in zip(membership, updated))
        membership = updated
        for cluster in range(5):
            rows = [
                row for row, member in zip(features, membership) if member == cluster
            ]
            if rows:
                for dimension in range(dimensions):
                    total = 0.0
                    for row in rows:
                        total = f32(total + row[dimension])
                    centers[cluster][dimension] = f32(total / len(rows))
        if changed <= 0.001:
            return iterations, centers
    raise ValueError("CPU Kmeans reference did not converge")


def nn(filelist):
    distances = []
    for filename in filelist.read_text().split():
        for row in (filelist.parent / filename).read_text().splitlines():
            if len(row) != 48:
                raise ValueError("unexpected NN record length")
            latitude = f32(float(row[28:33]))
            longitude = f32(float(row[33:38]))
            distances.append(math.hypot(latitude, longitude))
    return distances
