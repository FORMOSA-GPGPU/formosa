/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct ModelConfig {
  int32_t vocab_size;
  int32_t dim;
  int32_t hidden_dim;
  int32_t n_layers;
  int32_t n_heads;
  int32_t n_kv_heads;
  int32_t head_dim;
  int32_t padding;
};

struct LayerWeights {
  float *q_proj;
  size_t q_proj_size;
  float *k_proj;
  size_t k_proj_size;
  float *v_proj;
  size_t v_proj_size;
  float *o_proj;
  size_t o_proj_size;
  float *q_norm;
  size_t q_norm_size;
  float *k_norm;
  size_t k_norm_size;
  float *gate_proj;
  size_t gate_proj_size;
  float *up_proj;
  size_t up_proj_size;
  float *down_proj;
  size_t down_proj_size;
  float *input_layernorm;
  size_t input_layernorm_size;
  float *post_attn_layernorm;
  size_t post_attn_layernorm_size;
  float *pre_ff_layernorm;
  size_t pre_ff_layernorm_size;
  float *post_ff_layernorm;
  size_t post_ff_layernorm_size;
};

class ModelLoader {
 public:
  ModelLoader() = delete;
  ModelLoader(const std::string &path);
  ~ModelLoader();

  ModelConfig config;
  float *embed_tokens;
  size_t embed_tokens_size;
  std::vector<LayerWeights> layers;
  float *final_norm;
  size_t final_norm_size;

 private:
  int fd_;
  size_t size_;
  void *data_;
};
