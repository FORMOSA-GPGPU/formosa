// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

__kernel void update_kv_cache(__global float *kv_cache,
                              __global const float *current_kv,
                              const int seq_pos, const int max_seq_len,
                              const int num_kv_heads, const int head_dim) {
  int i = get_global_id(0);
  int total_elements = num_kv_heads * head_dim;

  if (i < total_elements) {
    int head_idx = i / head_dim;
    int dim_idx = i % head_dim;

    /* Calculate offset in the cache */
    int cache_idx =
        (head_idx * max_seq_len * head_dim) + (seq_pos * head_dim) + dim_idx;

    kv_cache[cache_idx] = current_kv[i];
  }
}
