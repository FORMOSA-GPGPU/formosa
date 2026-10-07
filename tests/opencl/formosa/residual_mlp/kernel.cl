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

__kernel void gelu_activation(__global const float *input,
                              __global float *output,
                              const int elements) {
  int gid = get_global_id(0);
  if (gid >= elements) return;
  float x = input[gid];
  float cubic = x * x * x;
  float inner = 0.7978845608f * (x + 0.044715f * cubic);
  output[gid] = 0.5f * x * (1.0f + tanh(inner));
}

__kernel void residual_add(__global const float *lhs,
                           __global const float *rhs,
                           __global float *output,
                           const int elements) {
  int gid = get_global_id(0);
  if (gid >= elements) return;
  output[gid] = lhs[gid] + rhs[gid];
}

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
  for (int i = 0; i < cols; ++i) mean += in_row[i];
  mean /= (float)cols;

  float var = 0.0f;
  for (int i = 0; i < cols; ++i) {
    float diff = in_row[i] - mean;
    var += diff * diff;
  }
  var /= (float)cols;

  float inv_std = rsqrt(var + eps);
  for (int i = 0; i < cols; ++i) {
    float norm = (in_row[i] - mean) * inv_std;
    out_row[i] = norm * gamma[i] + beta[i];
  }
}
