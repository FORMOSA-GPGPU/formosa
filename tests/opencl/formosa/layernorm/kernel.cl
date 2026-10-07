// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

__kernel void layernorm(__global const float *input,
                        __global const float *gamma,
                        __global const float *beta,
                        __global float *output,
                        const int cols,
                        const float eps) {
  int row = get_global_id(0);
  __global const float *in_row = input + row * cols;
  __global float *out_row = output + row * cols;

  float mean = 0.0f;
  for (int col = 0; col < cols; ++col) {
    mean += in_row[col];
  }
  mean /= (float)cols;

  float var = 0.0f;
  for (int col = 0; col < cols; ++col) {
    float diff = in_row[col] - mean;
    var += diff * diff;
  }
  var /= (float)cols;

  float inv_std = rsqrt(var + eps);
  for (int col = 0; col < cols; ++col) {
    float norm = (in_row[col] - mean) * inv_std;
    out_row[col] = norm * gamma[col] + beta[col];
  }
}
