// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

__kernel void qk_norm(__global const float *input,
                      __global const float *weights, __global float *output,
                      const int num_heads, const int head_dim,
                      const float epsilon) {
  /* One work item normalizes one head independently. */
  int h = get_global_id(0);
  if (h >= num_heads) return;

  int offset = h * head_dim;

  float sum_sq = 0.0f;
  for (int d = 0; d < head_dim; d++) {
    float val = input[offset + d];
    sum_sq += val * val;
  }

  float inv_rms = rsqrt(sum_sq / (float)head_dim + epsilon);
  for (int d = 0; d < head_dim; d++) {
    output[offset + d] = input[offset + d] * inv_rms * (weights[d] + 1.0f);
  }
}
