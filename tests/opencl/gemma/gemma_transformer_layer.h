/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <CL/opencl.hpp>
#include <string>

struct LayerConfig {
  int hidden_dim;
  int num_heads;
  int num_kv_heads;
  int head_dim;
  int mlp_hidden_dim;
  int max_seq_len;
  float epsilon;
  float theta_base;
  float attn_scale;
};

class GemmaTransformerLayer {
 public:
  GemmaTransformerLayer(cl::Context ctx, cl::CommandQueue cq,
                        const cl::Program &program, const LayerConfig &cfg,
                        cl::Buffer input_layernorm_w, cl::Buffer q_norm_w,
                        cl::Buffer k_norm_w, cl::Buffer q_proj_w,
                        cl::Buffer k_proj_w, cl::Buffer v_proj_w,
                        cl::Buffer o_proj_w,
                        cl::Buffer post_attention_layernorm_w,
                        cl::Buffer pre_feedforward_layernorm_w,
                        cl::Buffer post_feedforward_layernorm_w,
                        cl::Buffer gate_proj_w, cl::Buffer up_proj_w,
                        cl::Buffer down_proj_w);

  /* Enqueues the full transformer block and returns the output hidden state.
   * seq_len must equal seq_pos + 1 for autoregressive decoding. */
  const cl::Buffer &forward(const cl::Buffer &input, int seq_pos, int seq_len);

  /* Same computation as forward() but stalls after each stage and writes
   * every intermediate tensor to {stage_name}.dump, one float per line. */
  const cl::Buffer &forward_debug(const cl::Buffer &input, int seq_pos,
                                  int seq_len);

 private:
  cl::CommandQueue cq_;
  LayerConfig cfg_;

  /* Weight buffers */
  cl::Buffer input_layernorm_w_;
  cl::Buffer q_norm_w_;
  cl::Buffer k_norm_w_;
  cl::Buffer q_proj_w_;
  cl::Buffer k_proj_w_;
  cl::Buffer v_proj_w_;
  cl::Buffer o_proj_w_;
  cl::Buffer post_attention_layernorm_w_;
  cl::Buffer pre_feedforward_layernorm_w_;
  cl::Buffer post_feedforward_layernorm_w_;
  cl::Buffer gate_proj_w_;
  cl::Buffer up_proj_w_;
  cl::Buffer down_proj_w_;

  /* Kernels */
  cl::Kernel rmsnorm_k_;
  cl::Kernel qk_norm_k_;
  cl::Kernel gemm_k_;
  cl::Kernel rope_k_;
  cl::Kernel update_kv_cache_k_;
  cl::Kernel attention_k_;
  cl::Kernel geglu_k_;
  cl::Kernel geglu_gate_cubed_k_;
  cl::Kernel geglu_tanh_arg_k_;
  cl::Kernel geglu_tanh_k_;
  cl::Kernel geglu_gelu_k_;
  cl::Kernel residual_add_k_;

  /* Intermediate buffers — allocated once, reused every forward pass */
  cl::Buffer pre_att_norm_;
  cl::Buffer q_state_;
  cl::Buffer k_state_;
  cl::Buffer v_state_;
  cl::Buffer att_out_;
  cl::Buffer hidden_state_;
  cl::Buffer post_attn_normed_;
  cl::Buffer pre_mlp_norm_;
  cl::Buffer gate_state_;
  cl::Buffer up_state_;
  cl::Buffer gate_cubed_;
  cl::Buffer tanh_arg_;
  cl::Buffer tanh_state_;
  cl::Buffer gelu_state_;
  cl::Buffer mlp_act_;
  cl::Buffer mlp_out_;
  cl::Buffer scratch_scores_;

  /* Persistent KV cache for this layer */
  cl::Buffer k_cache_;
  cl::Buffer v_cache_;

  void enqueue_rmsnorm(const cl::Buffer &input, const cl::Buffer &weights,
                       cl::Buffer &output);
  void enqueue_qk_norm(cl::Buffer &buf, const cl::Buffer &weights,
                       int num_heads);
  void enqueue_gemm(cl::Buffer &C, const cl::Buffer &A, const cl::Buffer &B,
                    int M, int N, int K);
  void enqueue_rope(cl::Buffer &buf, int num_pairs, int seq_pos);
  void enqueue_update_kv_cache(cl::Buffer &cache, const cl::Buffer &current_kv,
                               int seq_pos);
  void enqueue_attention(int seq_len);
  void enqueue_geglu();
  void enqueue_geglu_gate_cubed();
  void enqueue_geglu_tanh_arg();
  void enqueue_geglu_tanh();
  void enqueue_geglu_gelu();
  void enqueue_residual_add(cl::Buffer &target, const cl::Buffer &residual);
  void dump_buffer(const cl::Buffer &buf, int num_floats,
                   const std::string &name);
};
