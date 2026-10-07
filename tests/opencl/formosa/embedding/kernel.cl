// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

__kernel void embedding_lookup(__global const float *table,
                               __global const int *indices,
                               __global float *output,
                               const int dim) {
  int gid = get_global_id(0);
  int token = gid / dim;
  int col = gid % dim;
  int idx = indices[token];
  output[gid] = table[idx * dim + col];
}
