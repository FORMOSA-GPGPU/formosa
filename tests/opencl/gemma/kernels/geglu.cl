// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

__kernel void geglu_gate_cubed(__global float *out, __global const float *gate,
                               const int hidden_dim) {
  int i = get_global_id(0);

  if (i < hidden_dim) {
    float x = gate[i];
    out[i] = x * x * x;
  }
}

__kernel void geglu_tanh_arg(__global float *out, __global const float *gate,
                             __global const float *gate_cubed,
                             const int hidden_dim) {
  int i = get_global_id(0);

  if (i < hidden_dim) {
    const float sqrt_2_over_pi = 0.79788456f;
    out[i] = sqrt_2_over_pi * (gate[i] + 0.044715f * gate_cubed[i]);
  }
}

__kernel void geglu_gelu(__global float *out, __global const float *gate,
                         __global const float *tanh_arg,
                         const int hidden_dim) {
  int i = get_global_id(0);

  if (i < hidden_dim) {
    float x = gate[i];
    out[i] = 0.5f * x * (1.0f + tanh(tanh_arg[i]));
  }
}

__kernel void geglu_tanh(__global float *out, __global const float *tanh_arg,
                         const int hidden_dim) {
  int i = get_global_id(0);

  if (i < hidden_dim) {
    out[i] = tanh(tanh_arg[i]);
  }
}

/* = GELU(gate) * up */
__kernel void geglu(__global float *out, __global const float *gate,
                    __global const float *up, const int hidden_dim) {
  int i = get_global_id(0);

  if (i < hidden_dim) {
    float x = gate[i];

    /* Approximate GELU: 0.5 * x * (1 + tanh(sqrt(2/pi) * (x + 0.044715 * x^3)))
     */
    const float sqrt_2_over_pi = 0.79788456f;
    float x_cubed = x * x * x;
    float tanh_arg = sqrt_2_over_pi * (x + 0.044715f * x_cubed);
    float gelu_x = 0.5f * x * (1.0f + tanh(tanh_arg));

    out[i] = gelu_x * up[i];
  }
}
