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

__kernel void project_output(__global const float *input,
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

__kernel void attention_scores(__global const float *query,
                               __global const float *key,
                               __global float *scores,
                               const int tokens,
                               const int head_dim,
                               const float scale) {
  int gid = get_global_id(0);
  int total = tokens * tokens;
  if (gid >= total) return;

  int q_row = gid / tokens;
  int k_row = gid % tokens;

  float sum = 0.0f;
  for (int i = 0; i < head_dim; ++i) {
    sum += query[q_row * head_dim + i] * key[k_row * head_dim + i];
  }
  scores[gid] = sum * scale;
}

__kernel void attention_softmax(__global const float *scores,
                                __global float *probs,
                                const int tokens) {
  int row = get_global_id(0);
  if (row >= tokens) return;

  __global const float *score_row = scores + row * tokens;
  __global float *prob_row = probs + row * tokens;

  float max_val = score_row[0];
  for (int col = 1; col < tokens; ++col) {
    if (score_row[col] > max_val) max_val = score_row[col];
  }

  float sum = 0.0f;
  for (int col = 0; col < tokens; ++col) {
    float value = exp(score_row[col] - max_val);
    prob_row[col] = value;
    sum += value;
  }

  for (int col = 0; col < tokens; ++col) {
    prob_row[col] /= sum;
  }
}

__kernel void attention_context(__global const float *probs,
                                __global const float *value,
                                __global float *context,
                                const int tokens,
                                const int head_dim) {
  int gid = get_global_id(0);
  int total = tokens * head_dim;
  if (gid >= total) return;

  int row = gid / head_dim;
  int col = gid % head_dim;

  float sum = 0.0f;
  for (int token = 0; token < tokens; ++token) {
    sum += probs[row * tokens + token] * value[token * head_dim + col];
  }
  context[gid] = sum;
}
