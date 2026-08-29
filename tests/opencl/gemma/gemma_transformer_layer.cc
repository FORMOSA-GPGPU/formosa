// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "gemma_transformer_layer.h"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
constexpr size_t kLocalSize1D = 64;
constexpr size_t kGemmLocalRows = 1;
constexpr size_t kGemmLocalCols = 64;

size_t round_up(size_t value, size_t multiple) {
  return ((value + multiple - 1) / multiple) * multiple;
}
}  // namespace

GemmaTransformerLayer::GemmaTransformerLayer(
    cl::Context ctx, cl::CommandQueue cq, const cl::Program &program,
    const LayerConfig &cfg, cl::Buffer input_layernorm_w, cl::Buffer q_norm_w,
    cl::Buffer k_norm_w, cl::Buffer q_proj_w, cl::Buffer k_proj_w,
    cl::Buffer v_proj_w, cl::Buffer o_proj_w,
    cl::Buffer post_attention_layernorm_w,
    cl::Buffer pre_feedforward_layernorm_w,
    cl::Buffer post_feedforward_layernorm_w, cl::Buffer gate_proj_w,
    cl::Buffer up_proj_w, cl::Buffer down_proj_w)
    : cq_(cq),
      cfg_(cfg),
      input_layernorm_w_(input_layernorm_w),
      q_norm_w_(q_norm_w),
      k_norm_w_(k_norm_w),
      q_proj_w_(q_proj_w),
      k_proj_w_(k_proj_w),
      v_proj_w_(v_proj_w),
      o_proj_w_(o_proj_w),
      post_attention_layernorm_w_(post_attention_layernorm_w),
      pre_feedforward_layernorm_w_(pre_feedforward_layernorm_w),
      post_feedforward_layernorm_w_(post_feedforward_layernorm_w),
      gate_proj_w_(gate_proj_w),
      up_proj_w_(up_proj_w),
      down_proj_w_(down_proj_w),
      rmsnorm_k_(program, "rmsnorm"),
      qk_norm_k_(program, "qk_norm"),
      gemm_k_(program, "gemm"),
      rope_k_(program, "rope"),
      update_kv_cache_k_(program, "update_kv_cache"),
      attention_k_(program, "attention_gqa"),
      geglu_k_(program, "geglu"),
      geglu_gate_cubed_k_(program, "geglu_gate_cubed"),
      geglu_tanh_arg_k_(program, "geglu_tanh_arg"),
      geglu_tanh_k_(program, "geglu_tanh"),
      geglu_gelu_k_(program, "geglu_gelu"),
      residual_add_k_(program, "residual_add") {
  const size_t fsize = sizeof(float);
  const int h = cfg_.hidden_dim;
  const int q_dim = cfg_.num_heads * cfg_.head_dim;
  const int kv_dim = cfg_.num_kv_heads * cfg_.head_dim;
  const int mlp = cfg_.mlp_hidden_dim;
  const int cache_elems = cfg_.num_kv_heads * cfg_.max_seq_len * cfg_.head_dim;

  pre_att_norm_ = cl::Buffer(ctx, CL_MEM_READ_WRITE, fsize * h);
  q_state_ = cl::Buffer(ctx, CL_MEM_READ_WRITE, fsize * q_dim);
  k_state_ = cl::Buffer(ctx, CL_MEM_READ_WRITE, fsize * kv_dim);
  v_state_ = cl::Buffer(ctx, CL_MEM_READ_WRITE, fsize * kv_dim);
  att_out_ = cl::Buffer(ctx, CL_MEM_READ_WRITE, fsize * q_dim);
  hidden_state_ = cl::Buffer(ctx, CL_MEM_READ_WRITE, fsize * h);
  post_attn_normed_ = cl::Buffer(ctx, CL_MEM_READ_WRITE, fsize * h);
  pre_mlp_norm_ = cl::Buffer(ctx, CL_MEM_READ_WRITE, fsize * h);
  gate_state_ = cl::Buffer(ctx, CL_MEM_READ_WRITE, fsize * mlp);
  up_state_ = cl::Buffer(ctx, CL_MEM_READ_WRITE, fsize * mlp);
  gate_cubed_ = cl::Buffer(ctx, CL_MEM_READ_WRITE, fsize * mlp);
  tanh_arg_ = cl::Buffer(ctx, CL_MEM_READ_WRITE, fsize * mlp);
  tanh_state_ = cl::Buffer(ctx, CL_MEM_READ_WRITE, fsize * mlp);
  gelu_state_ = cl::Buffer(ctx, CL_MEM_READ_WRITE, fsize * mlp);
  mlp_act_ = cl::Buffer(ctx, CL_MEM_READ_WRITE, fsize * mlp);
  mlp_out_ = cl::Buffer(ctx, CL_MEM_READ_WRITE, fsize * h);
  scratch_scores_ = cl::Buffer(ctx, CL_MEM_READ_WRITE,
                               fsize * cfg_.num_heads * cfg_.max_seq_len);
  k_cache_ = cl::Buffer(ctx, CL_MEM_READ_WRITE, fsize * cache_elems);
  v_cache_ = cl::Buffer(ctx, CL_MEM_READ_WRITE, fsize * cache_elems);
}

void GemmaTransformerLayer::enqueue_rmsnorm(const cl::Buffer &input,
                                            const cl::Buffer &weights,
                                            cl::Buffer &output) {
  rmsnorm_k_.setArg(0, input);
  rmsnorm_k_.setArg(1, weights);
  rmsnorm_k_.setArg(2, output);
  rmsnorm_k_.setArg(3, cfg_.hidden_dim);
  rmsnorm_k_.setArg(4, cfg_.epsilon);
  cq_.enqueueNDRangeKernel(rmsnorm_k_, cl::NullRange,
                           cl::NDRange(round_up(cfg_.hidden_dim, kLocalSize1D)),
                           cl::NDRange(kLocalSize1D));
}

void GemmaTransformerLayer::enqueue_qk_norm(cl::Buffer &buf,
                                            const cl::Buffer &weights,
                                            int num_heads) {
  /* In-place: non-overlapping per-head regions make aliased in/out safe. */
  qk_norm_k_.setArg(0, buf);
  qk_norm_k_.setArg(1, weights);
  qk_norm_k_.setArg(2, buf);
  qk_norm_k_.setArg(3, num_heads);
  qk_norm_k_.setArg(4, cfg_.head_dim);
  qk_norm_k_.setArg(5, cfg_.epsilon);
  cq_.enqueueNDRangeKernel(qk_norm_k_, cl::NullRange,
                           cl::NDRange(round_up(num_heads, kLocalSize1D)),
                           cl::NDRange(kLocalSize1D));
}

void GemmaTransformerLayer::enqueue_gemm(cl::Buffer &C, const cl::Buffer &A,
                                         const cl::Buffer &B, int M, int N,
                                         int K) {
  gemm_k_.setArg(0, C);
  gemm_k_.setArg(1, A);
  gemm_k_.setArg(2, B);
  gemm_k_.setArg(3, M);
  gemm_k_.setArg(4, N);
  gemm_k_.setArg(5, K);
  cq_.enqueueNDRangeKernel(
      gemm_k_, cl::NullRange,
      cl::NDRange(round_up(M, kGemmLocalRows), round_up(N, kGemmLocalCols)),
      cl::NDRange(kGemmLocalRows, kGemmLocalCols));
}

void GemmaTransformerLayer::enqueue_rope(cl::Buffer &buf, int num_pairs,
                                         int seq_pos) {
  /* In-place: each work item reads its pair into locals before writing,
   * so aliased output/input pointers are safe here. */
  rope_k_.setArg(0, buf);
  rope_k_.setArg(1, buf);
  rope_k_.setArg(2, num_pairs);
  rope_k_.setArg(3, cfg_.head_dim);
  rope_k_.setArg(4, seq_pos);
  rope_k_.setArg(5, cfg_.theta_base);
  cq_.enqueueNDRangeKernel(rope_k_, cl::NullRange,
                           cl::NDRange(round_up(num_pairs, kLocalSize1D)),
                           cl::NDRange(kLocalSize1D));
}

void GemmaTransformerLayer::enqueue_update_kv_cache(
    cl::Buffer &cache, const cl::Buffer &current_kv, int seq_pos) {
  update_kv_cache_k_.setArg(0, cache);
  update_kv_cache_k_.setArg(1, current_kv);
  update_kv_cache_k_.setArg(2, seq_pos);
  update_kv_cache_k_.setArg(3, cfg_.max_seq_len);
  update_kv_cache_k_.setArg(4, cfg_.num_kv_heads);
  update_kv_cache_k_.setArg(5, cfg_.head_dim);
  const int elems = cfg_.num_kv_heads * cfg_.head_dim;
  cq_.enqueueNDRangeKernel(update_kv_cache_k_, cl::NullRange,
                           cl::NDRange(round_up(elems, kLocalSize1D)),
                           cl::NDRange(kLocalSize1D));
}

void GemmaTransformerLayer::enqueue_attention(int seq_len) {
  attention_k_.setArg(0, att_out_);
  attention_k_.setArg(1, q_state_);
  attention_k_.setArg(2, k_cache_);
  attention_k_.setArg(3, v_cache_);
  attention_k_.setArg(4, scratch_scores_);
  attention_k_.setArg(5, cfg_.num_heads);
  attention_k_.setArg(6, cfg_.num_kv_heads);
  attention_k_.setArg(7, seq_len);
  attention_k_.setArg(8, cfg_.max_seq_len);
  attention_k_.setArg(9, cfg_.head_dim);
  attention_k_.setArg(10, cfg_.attn_scale);
  cq_.enqueueNDRangeKernel(attention_k_, cl::NullRange,
                           cl::NDRange(round_up(cfg_.num_heads, kLocalSize1D)),
                           cl::NDRange(kLocalSize1D));
}

void GemmaTransformerLayer::enqueue_geglu() {
  geglu_k_.setArg(0, mlp_act_);
  geglu_k_.setArg(1, gate_state_);
  geglu_k_.setArg(2, up_state_);
  geglu_k_.setArg(3, cfg_.mlp_hidden_dim);
  cq_.enqueueNDRangeKernel(
      geglu_k_, cl::NullRange,
      cl::NDRange(round_up(cfg_.mlp_hidden_dim, kLocalSize1D)),
      cl::NDRange(kLocalSize1D));
}

void GemmaTransformerLayer::enqueue_geglu_gate_cubed() {
  geglu_gate_cubed_k_.setArg(0, gate_cubed_);
  geglu_gate_cubed_k_.setArg(1, gate_state_);
  geglu_gate_cubed_k_.setArg(2, cfg_.mlp_hidden_dim);
  cq_.enqueueNDRangeKernel(
      geglu_gate_cubed_k_, cl::NullRange,
      cl::NDRange(round_up(cfg_.mlp_hidden_dim, kLocalSize1D)),
      cl::NDRange(kLocalSize1D));
}

void GemmaTransformerLayer::enqueue_geglu_tanh_arg() {
  geglu_tanh_arg_k_.setArg(0, tanh_arg_);
  geglu_tanh_arg_k_.setArg(1, gate_state_);
  geglu_tanh_arg_k_.setArg(2, gate_cubed_);
  geglu_tanh_arg_k_.setArg(3, cfg_.mlp_hidden_dim);
  cq_.enqueueNDRangeKernel(
      geglu_tanh_arg_k_, cl::NullRange,
      cl::NDRange(round_up(cfg_.mlp_hidden_dim, kLocalSize1D)),
      cl::NDRange(kLocalSize1D));
}

void GemmaTransformerLayer::enqueue_geglu_tanh() {
  geglu_tanh_k_.setArg(0, tanh_state_);
  geglu_tanh_k_.setArg(1, tanh_arg_);
  geglu_tanh_k_.setArg(2, cfg_.mlp_hidden_dim);
  cq_.enqueueNDRangeKernel(
      geglu_tanh_k_, cl::NullRange,
      cl::NDRange(round_up(cfg_.mlp_hidden_dim, kLocalSize1D)),
      cl::NDRange(kLocalSize1D));
}

void GemmaTransformerLayer::enqueue_geglu_gelu() {
  geglu_gelu_k_.setArg(0, gelu_state_);
  geglu_gelu_k_.setArg(1, gate_state_);
  geglu_gelu_k_.setArg(2, tanh_arg_);
  geglu_gelu_k_.setArg(3, cfg_.mlp_hidden_dim);
  cq_.enqueueNDRangeKernel(
      geglu_gelu_k_, cl::NullRange,
      cl::NDRange(round_up(cfg_.mlp_hidden_dim, kLocalSize1D)),
      cl::NDRange(kLocalSize1D));
}

void GemmaTransformerLayer::enqueue_residual_add(cl::Buffer &target,
                                                 const cl::Buffer &residual) {
  residual_add_k_.setArg(0, target);
  residual_add_k_.setArg(1, residual);
  residual_add_k_.setArg(2, cfg_.hidden_dim);
  cq_.enqueueNDRangeKernel(residual_add_k_, cl::NullRange,
                           cl::NDRange(round_up(cfg_.hidden_dim, kLocalSize1D)),
                           cl::NDRange(kLocalSize1D));
}

const cl::Buffer &GemmaTransformerLayer::forward(const cl::Buffer &input,
                                                 int seq_pos, int seq_len) {
  const int q_dim = cfg_.num_heads * cfg_.head_dim;
  const int kv_dim = cfg_.num_kv_heads * cfg_.head_dim;

  /* RMSNorm: input -> pre_att_norm */
  enqueue_rmsnorm(input, input_layernorm_w_, pre_att_norm_);

  /* Q/K/V projections */
  enqueue_gemm(q_state_, pre_att_norm_, q_proj_w_, 1, q_dim, cfg_.hidden_dim);
  enqueue_gemm(k_state_, pre_att_norm_, k_proj_w_, 1, kv_dim, cfg_.hidden_dim);
  enqueue_gemm(v_state_, pre_att_norm_, v_proj_w_, 1, kv_dim, cfg_.hidden_dim);

  /* Per-head QK normalization before RoPE */
  enqueue_qk_norm(q_state_, q_norm_w_, cfg_.num_heads);
  enqueue_qk_norm(k_state_, k_norm_w_, cfg_.num_kv_heads);

  /* Rotary position encoding */
  enqueue_rope(q_state_, cfg_.num_heads * cfg_.head_dim / 2, seq_pos);
  enqueue_rope(k_state_, cfg_.num_kv_heads * cfg_.head_dim / 2, seq_pos);

  /* Write current K and V into the persistent cache */
  enqueue_update_kv_cache(k_cache_, k_state_, seq_pos);
  enqueue_update_kv_cache(v_cache_, v_state_, seq_pos);

  /* GQA attention */
  enqueue_attention(seq_len);

  /* O projection: att_out * o_proj -> hidden_state */
  enqueue_gemm(hidden_state_, att_out_, o_proj_w_, 1, cfg_.hidden_dim, q_dim);

  /* Post-attention norm applied to o_proj output before first residual add */
  enqueue_rmsnorm(hidden_state_, post_attention_layernorm_w_,
                  post_attn_normed_);

  /* First residual add: post_attn_normed = post_attn_normed + input */
  enqueue_residual_add(post_attn_normed_, input);

  /* Pre-feedforward RMSNorm */
  enqueue_rmsnorm(post_attn_normed_, pre_feedforward_layernorm_w_,
                  pre_mlp_norm_);

  /* Gate and Up projections */
  enqueue_gemm(gate_state_, pre_mlp_norm_, gate_proj_w_, 1, cfg_.mlp_hidden_dim,
               cfg_.hidden_dim);
  enqueue_gemm(up_state_, pre_mlp_norm_, up_proj_w_, 1, cfg_.mlp_hidden_dim,
               cfg_.hidden_dim);

  /* GeGLU activation */
  enqueue_geglu();

  /* Down projection: mlp_act * down_proj -> mlp_out */
  enqueue_gemm(mlp_out_, mlp_act_, down_proj_w_, 1, cfg_.hidden_dim,
               cfg_.mlp_hidden_dim);

  /* Post-feedforward norm applied to mlp_out before second residual add */
  enqueue_rmsnorm(mlp_out_, post_feedforward_layernorm_w_, hidden_state_);

  /* Second residual add: hidden_state = hidden_state + post_attn_normed */
  enqueue_residual_add(hidden_state_, post_attn_normed_);

  return hidden_state_;
}

void GemmaTransformerLayer::dump_buffer(const cl::Buffer &buf, int num_floats,
                                        const std::string &name) {
  std::vector<float> host(num_floats);
  cq_.enqueueReadBuffer(buf, CL_TRUE, 0, sizeof(float) * num_floats,
                        host.data());

  std::ofstream f(name + ".dump");
  if (!f) {
    throw std::runtime_error("failed to open " + name + ".dump");
  }
  f << std::fixed
    << std::setprecision(std::numeric_limits<float>::max_digits10);
  for (float v : host) {
    f << v << "\n";
  }
}

const cl::Buffer &GemmaTransformerLayer::forward_debug(const cl::Buffer &input,
                                                       int seq_pos,
                                                       int seq_len) {
  const int q_dim = cfg_.num_heads * cfg_.head_dim;
  const int kv_dim = cfg_.num_kv_heads * cfg_.head_dim;

  std::cout << "pre_att_norm\n";
  enqueue_rmsnorm(input, input_layernorm_w_, pre_att_norm_);
  cq_.finish();
  dump_buffer(pre_att_norm_, cfg_.hidden_dim, "pre_att_norm");

  std::cout << "q_proj\n";
  enqueue_gemm(q_state_, pre_att_norm_, q_proj_w_, 1, q_dim, cfg_.hidden_dim);
  cq_.finish();
  dump_buffer(q_state_, q_dim, "q_proj");

  std::cout << "k_proj\n";
  enqueue_gemm(k_state_, pre_att_norm_, k_proj_w_, 1, kv_dim, cfg_.hidden_dim);
  cq_.finish();
  dump_buffer(k_state_, kv_dim, "k_proj");

  std::cout << "v_proj\n";
  enqueue_gemm(v_state_, pre_att_norm_, v_proj_w_, 1, kv_dim, cfg_.hidden_dim);
  cq_.finish();
  dump_buffer(v_state_, kv_dim, "v_proj");

  std::cout << "q_qknorm\n";
  enqueue_qk_norm(q_state_, q_norm_w_, cfg_.num_heads);
  cq_.finish();
  dump_buffer(q_state_, q_dim, "q_qknorm");

  std::cout << "k_qknorm\n";
  enqueue_qk_norm(k_state_, k_norm_w_, cfg_.num_kv_heads);
  cq_.finish();
  dump_buffer(k_state_, kv_dim, "k_qknorm");

  std::cout << "q_rope\n";
  enqueue_rope(q_state_, cfg_.num_heads * cfg_.head_dim / 2, seq_pos);
  cq_.finish();
  dump_buffer(q_state_, q_dim, "q_rope");

  std::cout << "k_rope\n";
  enqueue_rope(k_state_, cfg_.num_kv_heads * cfg_.head_dim / 2, seq_pos);
  cq_.finish();
  dump_buffer(k_state_, kv_dim, "k_rope");

  std::cout << "k_cache_update\n";
  enqueue_update_kv_cache(k_cache_, k_state_, seq_pos);
  std::cout << "v_cache_update\n";
  enqueue_update_kv_cache(v_cache_, v_state_, seq_pos);
  cq_.finish();

  std::cout << "att_out\n";
  enqueue_attention(seq_len);
  cq_.finish();
  dump_buffer(att_out_, q_dim, "att_out");

  std::cout << "o_proj\n";
  enqueue_gemm(hidden_state_, att_out_, o_proj_w_, 1, cfg_.hidden_dim, q_dim);
  cq_.finish();
  dump_buffer(hidden_state_, cfg_.hidden_dim, "o_proj");

  std::cout << "post_attn_norm\n";
  enqueue_rmsnorm(hidden_state_, post_attention_layernorm_w_,
                  post_attn_normed_);
  cq_.finish();
  dump_buffer(post_attn_normed_, cfg_.hidden_dim, "post_attn_norm");

  std::cout << "post_attn_residual\n";
  enqueue_residual_add(post_attn_normed_, input);
  cq_.finish();
  dump_buffer(post_attn_normed_, cfg_.hidden_dim, "post_attn_residual");

  std::cout << "pre_mlp_norm\n";
  enqueue_rmsnorm(post_attn_normed_, pre_feedforward_layernorm_w_,
                  pre_mlp_norm_);
  cq_.finish();
  dump_buffer(pre_mlp_norm_, cfg_.hidden_dim, "pre_mlp_norm");

  std::cout << "gate_proj\n";
  enqueue_gemm(gate_state_, pre_mlp_norm_, gate_proj_w_, 1, cfg_.mlp_hidden_dim,
               cfg_.hidden_dim);
  cq_.finish();
  dump_buffer(gate_state_, cfg_.mlp_hidden_dim, "gate_proj");

  std::cout << "up_proj\n";
  enqueue_gemm(up_state_, pre_mlp_norm_, up_proj_w_, 1, cfg_.mlp_hidden_dim,
               cfg_.hidden_dim);
  cq_.finish();
  dump_buffer(up_state_, cfg_.mlp_hidden_dim, "up_proj");

  std::cout << "mlp_gate_cubed\n";
  enqueue_geglu_gate_cubed();
  cq_.finish();
  dump_buffer(gate_cubed_, cfg_.mlp_hidden_dim, "mlp_gate_cubed");

  std::cout << "mlp_tanh_arg\n";
  enqueue_geglu_tanh_arg();
  cq_.finish();
  dump_buffer(tanh_arg_, cfg_.mlp_hidden_dim, "mlp_tanh_arg");

  std::cout << "mlp_tanh\n";
  enqueue_geglu_tanh();
  cq_.finish();
  dump_buffer(tanh_state_, cfg_.mlp_hidden_dim, "mlp_tanh");

  std::cout << "mlp_gelu\n";
  enqueue_geglu_gelu();
  cq_.finish();
  dump_buffer(gelu_state_, cfg_.mlp_hidden_dim, "mlp_gelu");

  std::cout << "mlp_act\n";
  enqueue_geglu();
  cq_.finish();
  dump_buffer(mlp_act_, cfg_.mlp_hidden_dim, "mlp_act");

  std::cout << "down_proj\n";
  enqueue_gemm(mlp_out_, mlp_act_, down_proj_w_, 1, cfg_.hidden_dim,
               cfg_.mlp_hidden_dim);
  cq_.finish();
  dump_buffer(mlp_out_, cfg_.hidden_dim, "down_proj");

  std::cout << "post_ff_norm\n";
  enqueue_rmsnorm(mlp_out_, post_feedforward_layernorm_w_, hidden_state_);
  cq_.finish();
  dump_buffer(hidden_state_, cfg_.hidden_dim, "post_ff_norm");

  std::cout << "output\n";
  enqueue_residual_add(hidden_state_, post_attn_normed_);
  cq_.finish();
  dump_buffer(hidden_state_, cfg_.hidden_dim, "output");

  return hidden_state_;
}
