// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

__kernel void rmsnorm(__global const float *input,
                      __global const float *gamma,
                      __global float *output,
                      const int cols,
                      const float eps) {
  int row = get_global_id(0);
  __global const float *in_row = input + row * cols;
  __global float *out_row = output + row * cols;

  float mean_sq = 0.0f;
  for (int col = 0; col < cols; ++col) {
    mean_sq += in_row[col] * in_row[col];
  }
  mean_sq /= (float)cols;

  float inv_rms = rsqrt(mean_sq + eps);
  for (int col = 0; col < cols; ++col) {
    out_row[col] = in_row[col] * inv_rms * gamma[col];
  }
}
