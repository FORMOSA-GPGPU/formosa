// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

__kernel void softmax_rows(__global const float *input,
                           __global float *output,
                           const int cols) {
  int row = get_global_id(0);
  __global const float *in_row = input + row * cols;
  __global float *out_row = output + row * cols;

  float max_val = in_row[0];
  for (int col = 1; col < cols; ++col) {
    if (in_row[col] > max_val) max_val = in_row[col];
  }

  float sum = 0.0f;
  for (int col = 0; col < cols; ++col) {
    float value = exp(in_row[col] - max_val);
    out_row[col] = value;
    sum += value;
  }

  for (int col = 0; col < cols; ++col) {
    out_row[col] /= sum;
  }
}
