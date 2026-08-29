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

__kernel void attention_scores(__global const float *query,
                               __global const float *key,
                               __global float *scores,
                               const int tokens,
                               const int head_dim,
                               const float scale) {
  int gid = get_global_id(0);
  int total = tokens * tokens;
  if (gid >= total) return;

  int row = gid / tokens;
  int col = gid % tokens;
  float sum = 0.0f;
  for (int i = 0; i < head_dim; ++i) {
    sum += query[row * head_dim + i] * key[col * head_dim + i];
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
  for (int i = 1; i < tokens; ++i) {
    if (score_row[i] > max_val) max_val = score_row[i];
  }

  float sum = 0.0f;
  for (int i = 0; i < tokens; ++i) {
    float value = exp(score_row[i] - max_val);
    prob_row[i] = value;
    sum += value;
  }

  for (int i = 0; i < tokens; ++i) {
    prob_row[i] /= sum;
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
  for (int i = 0; i < tokens; ++i) {
    sum += probs[row * tokens + i] * value[i * head_dim + col];
  }
  context[gid] = sum;
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
