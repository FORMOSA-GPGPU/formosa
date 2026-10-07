// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

__kernel void bias_relu(__global const float *input,
                        __global const float *bias,
                        __global float *output,
                        const int cols) {
  int gid = get_global_id(0);
  int col = gid % cols;
  float x = input[gid] + bias[col];
  output[gid] = x > 0.0f ? x : 0.0f;
}
