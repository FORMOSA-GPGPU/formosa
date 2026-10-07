// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

__kernel void project_matrix(__global const float *input,
                             __global const float *weights,
                             __global const float *bias,
                             __global float *output,
                             const int rows,
                             const int input_cols,
                             const int output_cols) {
  int gid = get_global_id(0);
  int total = rows * output_cols;
  if (gid >= total) return;
  int row = gid / output_cols;
  int col = gid % output_cols;
  float sum = bias[col];
  for (int i = 0; i < input_cols; ++i) {
    sum += input[row * input_cols + i] * weights[i * output_cols + col];
  }
  output[gid] = sum;
}

__kernel void silu_activation(__global const float *input,
                              __global float *output,
                              const int elements) {
  int gid = get_global_id(0);
  if (gid >= elements) return;
  float x = input[gid];
  output[gid] = x / (1.0f + exp(-x));
}

__kernel void elementwise_mul(__global const float *lhs,
                              __global const float *rhs,
                              __global float *output,
                              const int elements) {
  int gid = get_global_id(0);
  if (gid >= elements) return;
  output[gid] = lhs[gid] * rhs[gid];
}

__kernel void residual_add(__global const float *lhs,
                           __global const float *rhs,
                           __global float *output,
                           const int elements) {
  int gid = get_global_id(0);
  if (gid >= elements) return;
  output[gid] = lhs[gid] + rhs[gid];
}

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
