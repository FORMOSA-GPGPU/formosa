// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

/* One thread rotates one split-half RoPE pair. */
__kernel void rope(__global float *output, __global const float *input,
                   const int num_pairs, const int head_dim, const int seq_pos,
                   const float theta_base) {
  const int i = get_global_id(0);

  if (i < num_pairs) {
    const int half_dim = head_dim / 2;
    const int head_idx = i / half_dim;
    const int dim_idx = i % half_dim;
    const int first_idx = head_idx * head_dim + dim_idx;
    const int second_idx = first_idx + half_dim;

    const float inv_freq =
        pow(theta_base, -((float)(dim_idx * 2) / (float)head_dim));
    const float theta = (float)seq_pos * inv_freq;

    float cos_theta, sin_theta;
    sin_theta = sincos(theta, &cos_theta);

    const float x_first = input[first_idx];
    const float x_second = input[second_idx];

    output[first_idx] = x_first * cos_theta - x_second * sin_theta;
    output[second_idx] = x_second * cos_theta + x_first * sin_theta;
  }
}
