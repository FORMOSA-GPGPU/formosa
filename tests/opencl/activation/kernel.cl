// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#pragma OPENCL EXTENSION cl_khr_fp64 : enable

__kernel void relu_kernel(__global const float *input, __global float *output) {
  int gid = get_global_id(0);
  float x = input[gid];
  output[gid] = x > 0.0f ? x : 0.0f;
}

__kernel void leaky_relu_kernel(__global const float *input,
                                __global float *output,
                                const float alpha) {
  int gid = get_global_id(0);
  float x = input[gid];
  output[gid] = x > 0.0f ? x : alpha * x;
}

__kernel void sigmoid_kernel(__global const float *input,
                             __global float *output) {
  int gid = get_global_id(0);
  float x = input[gid];
  output[gid] = 1.0f / (1.0f + exp(-x));
}

__kernel void tanh_kernel(__global const float *input, __global float *output) {
  int gid = get_global_id(0);
  output[gid] = tanh(input[gid]);
}

__kernel void gelu_kernel(__global const float *input, __global float *output) {
  int gid = get_global_id(0);
  float x = input[gid];
  float cdf = 0.5f * (1.0f + tanh(0.7978845608f *
                                  (x + 0.044715f * x * x * x)));
  output[gid] = x * cdf;
}
