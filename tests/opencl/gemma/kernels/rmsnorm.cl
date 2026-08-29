// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

__kernel void rmsnorm(__global const float *input,
                      __global const float *norm_weights,
                      __global float *output, const int hidden_dim,
                      const float epsilon) {
  /* One thread -> one hidden output element */
  int i = get_global_id(0);

  if (i < hidden_dim) {
    float sum_sq = 0.0f;
    for (int j = 0; j < hidden_dim; j++) {
      float val = input[j];
      sum_sq += val * val;
    }

    float mean_sq = sum_sq / (float)hidden_dim;
    float inv_rms = rsqrt(mean_sq + epsilon);

    output[i] = input[i] * inv_rms * (norm_weights[i] + 1.0f);
  }
}
