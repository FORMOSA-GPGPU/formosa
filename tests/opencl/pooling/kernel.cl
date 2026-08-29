// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

__kernel void max_pool2d(__global const float *input,
                         __global float *output,
                         const int input_w,
                         const int pool,
                         const int stride,
                         const int output_w) {
  int gid = get_global_id(0);
  int row = gid / output_w;
  int col = gid % output_w;
  float max_val = input[(row * stride) * input_w + col * stride];
  for (int i = 0; i < pool; ++i) {
    for (int j = 0; j < pool; ++j) {
      int in_row = row * stride + i;
      int in_col = col * stride + j;
      float value = input[in_row * input_w + in_col];
      if (value > max_val) max_val = value;
    }
  }
  output[gid] = max_val;
}

__kernel void avg_pool2d(__global const float *input,
                         __global float *output,
                         const int input_w,
                         const int pool,
                         const int stride,
                         const int output_w) {
  int gid = get_global_id(0);
  int row = gid / output_w;
  int col = gid % output_w;
  float sum = 0.0f;
  for (int i = 0; i < pool; ++i) {
    for (int j = 0; j < pool; ++j) {
      int in_row = row * stride + i;
      int in_col = col * stride + j;
      sum += input[in_row * input_w + in_col];
    }
  }
  output[gid] = sum / (float)(pool * pool);
}
