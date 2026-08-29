// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "model_loader.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

ModelLoader::ModelLoader(const std::string &path) {
  fd_ = open(path.c_str(), O_RDONLY);
  if (fd_ == -1) {
    std::fprintf(stderr, "Error opening file %s: %s\n", path.c_str(),
                 std::strerror(errno));
    abort();
  }

  size_ = lseek(fd_, 0, SEEK_END);
  lseek(fd_, 0, SEEK_SET);

  data_ = mmap(NULL, size_, PROT_READ, MAP_PRIVATE, fd_, 0);
  if (data_ == MAP_FAILED) {
    std::fprintf(stderr, "Map failed: %s (errno: %d)\n", std::strerror(errno),
                 errno);
    abort();
  }

  config = *static_cast<ModelConfig *>(data_);
  float *ptr = reinterpret_cast<float *>(static_cast<uint8_t *>(data_) +
                                         sizeof(ModelConfig));

  std::printf("Model %s Config:\n", path.c_str());
  std::printf("  vocab_size: %d\n", config.vocab_size);
  std::printf("  dim: %d\n", config.dim);
  std::printf("  hidden_dim: %d\n", config.hidden_dim);
  std::printf("  n_layers: %d\n", config.n_layers);
  std::printf("  n_heads: %d\n", config.n_heads);
  std::printf("  n_kv_heads: %d\n", config.n_kv_heads);
  std::printf("  head_dim: %d\n", config.head_dim);

  embed_tokens = ptr;
  embed_tokens_size = config.vocab_size * config.dim;
  ptr += embed_tokens_size;

  layers.resize(config.n_layers);
  for (int i = 0; i < config.n_layers; i++) {
    LayerWeights &l = layers[i];

    l.q_proj = ptr;
    l.q_proj_size = config.n_heads * config.head_dim * config.dim;
    ptr += l.q_proj_size;

    l.k_proj = ptr;
    l.k_proj_size = config.n_kv_heads * config.head_dim * config.dim;
    ptr += l.k_proj_size;

    l.v_proj = ptr;
    l.v_proj_size = config.n_kv_heads * config.head_dim * config.dim;
    ptr += l.v_proj_size;

    l.o_proj = ptr;
    l.o_proj_size = config.dim * config.n_heads * config.head_dim;
    ptr += l.o_proj_size;

    l.q_norm = ptr;
    l.q_norm_size = config.head_dim;
    ptr += l.q_norm_size;

    l.k_norm = ptr;
    l.k_norm_size = config.head_dim;
    ptr += l.k_norm_size;

    l.gate_proj = ptr;
    l.gate_proj_size = config.hidden_dim * config.dim;
    ptr += l.gate_proj_size;

    l.up_proj = ptr;
    l.up_proj_size = config.hidden_dim * config.dim;
    ptr += l.up_proj_size;

    l.down_proj = ptr;
    l.down_proj_size = config.dim * config.hidden_dim;
    ptr += l.down_proj_size;

    l.input_layernorm = ptr;
    l.input_layernorm_size = config.dim;
    ptr += l.input_layernorm_size;

    l.post_attn_layernorm = ptr;
    l.post_attn_layernorm_size = config.dim;
    ptr += l.post_attn_layernorm_size;

    l.pre_ff_layernorm = ptr;
    l.pre_ff_layernorm_size = config.dim;
    ptr += l.pre_ff_layernorm_size;

    l.post_ff_layernorm = ptr;
    l.post_ff_layernorm_size = config.dim;
    ptr += l.post_ff_layernorm_size;
  }

  final_norm = ptr;
  final_norm_size = config.dim;
}

ModelLoader::~ModelLoader() {
  munmap(data_, size_);
  close(fd_);
}
