// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

/* target = target + residual */
__kernel void residual_add(__global float *target,
                           __global const float *residual,
                           const int hidden_dim) {
  int i = get_global_id(0);

  if (i < hidden_dim) {
    target[i] += residual[i];
  }
}
