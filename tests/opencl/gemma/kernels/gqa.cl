// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

__kernel void attention_gqa(__global float *out, __global const float *q,
                            __global const float *k_cache,
                            __global const float *v_cache,
                            __global float *scratch_scores,

                            const int num_heads, const int num_kv_heads,
                            const int seq_len, const int max_seq_len,
                            const int head_dim, const float scale) {
  int head_idx = get_global_id(0);
  if (head_idx >= num_heads) return;

  int kv_groups = num_heads / num_kv_heads;
  int kv_head_idx = head_idx / kv_groups;

  int q_offset = head_idx * head_dim;
  int kv_base_offset = kv_head_idx * max_seq_len * head_dim;
  int score_offset = head_idx * max_seq_len;

  float max_score = -INFINITY;

  for (int t = 0; t < seq_len; t++) {
    float dot_prod = 0.0f;
    int k_offset = kv_base_offset + t * head_dim;

    for (int d = 0; d < head_dim; d++) {
      dot_prod += q[q_offset + d] * k_cache[k_offset + d];
    }

    float score = dot_prod * scale;
    scratch_scores[score_offset + t] = score;

    if (score > max_score) {
      max_score = score;
    }
  }

  float sum_exp = 0.0f;
  for (int t = 0; t < seq_len; t++) {
    float exp_val = exp(scratch_scores[score_offset + t] - max_score);
    scratch_scores[score_offset + t] = exp_val;
    sum_exp += exp_val;
  }

  for (int t = 0; t < seq_len; t++) {
    scratch_scores[score_offset + t] /= sum_exp;
  }

  for (int d = 0; d < head_dim; d++) {
    float out_val = 0.0f;

    for (int t = 0; t < seq_len; t++) {
      int v_offset = kv_base_offset + t * head_dim;
      float weight = scratch_scores[score_offset + t];

      out_val += weight * v_cache[v_offset + d];
    }

    out[q_offset + d] = out_val;
  }
}
