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

__kernel void sigmoid_activation(__global const float *input,
                                 __global float *output,
                                 const int elements) {
  int gid = get_global_id(0);
  if (gid >= elements) return;
  output[gid] = 1.0f / (1.0f + exp(-input[gid]));
}

__kernel void tanh_activation(__global const float *input,
                              __global float *output,
                              const int elements) {
  int gid = get_global_id(0);
  if (gid >= elements) return;
  output[gid] = tanh(input[gid]);
}

__kernel void combine_reset_candidate(__global const float *reset_gate,
                                      __global const float *hidden_proj,
                                      __global float *output,
                                      const int elements) {
  int gid = get_global_id(0);
  if (gid >= elements) return;
  output[gid] = reset_gate[gid] * hidden_proj[gid];
}

__kernel void gru_hidden_update(__global const float *update_gate,
                                __global const float *candidate,
                                __global const float *hidden_prev,
                                __global float *hidden_next,
                                const int elements) {
  int gid = get_global_id(0);
  if (gid >= elements) return;
  float z = update_gate[gid];
  hidden_next[gid] = (1.0f - z) * candidate[gid] + z * hidden_prev[gid];
}
