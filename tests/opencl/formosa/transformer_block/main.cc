// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <CL/opencl.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#ifndef KERNEL_PATH
#define KERNEL_PATH "./kernel.cl"
#endif

#define TOKENS 32
#define MODEL_DIM 32
#define HEAD_DIM 32
#define HIDDEN_DIM 128
#define EPS 1e-5f
#define FP_ERROR 0.01f

#define RED "\033[0;31m"
#define GREEN "\033[0;32m"
#define RESET "\033[0m"

static void linear_cpu(const std::vector<float> &input,
                       const std::vector<float> &weights,
                       const std::vector<float> &bias,
                       std::vector<float> &output, int rows, int input_cols,
                       int output_cols) {
  for (int row = 0; row < rows; ++row) {
    for (int col = 0; col < output_cols; ++col) {
      float sum = bias[col];
      for (int i = 0; i < input_cols; ++i) {
        sum += input[row * input_cols + i] * weights[i * output_cols + col];
      }
      output[row * output_cols + col] = sum;
    }
  }
}

static void attention_scores_cpu(const std::vector<float> &query,
                                 const std::vector<float> &key,
                                 std::vector<float> &scores) {
  const float scale = 1.0f / std::sqrt(static_cast<float>(HEAD_DIM));
  for (int row = 0; row < TOKENS; ++row) {
    for (int col = 0; col < TOKENS; ++col) {
      float sum = 0.0f;
      for (int i = 0; i < HEAD_DIM; ++i) {
        sum += query[row * HEAD_DIM + i] * key[col * HEAD_DIM + i];
      }
      scores[row * TOKENS + col] = sum * scale;
    }
  }
}

static void softmax_cpu(const std::vector<float> &scores,
                        std::vector<float> &probs) {
  for (int row = 0; row < TOKENS; ++row) {
    float max_val = scores[row * TOKENS];
    for (int i = 1; i < TOKENS; ++i) {
      max_val = std::max(max_val, scores[row * TOKENS + i]);
    }
    float sum = 0.0f;
    for (int i = 0; i < TOKENS; ++i) {
      float value = std::exp(scores[row * TOKENS + i] - max_val);
      probs[row * TOKENS + i] = value;
      sum += value;
    }
    for (int i = 0; i < TOKENS; ++i) probs[row * TOKENS + i] /= sum;
  }
}

static void context_cpu(const std::vector<float> &probs,
                        const std::vector<float> &value,
                        std::vector<float> &context) {
  for (int row = 0; row < TOKENS; ++row) {
    for (int col = 0; col < HEAD_DIM; ++col) {
      float sum = 0.0f;
      for (int i = 0; i < TOKENS; ++i) {
        sum += probs[row * TOKENS + i] * value[i * HEAD_DIM + col];
      }
      context[row * HEAD_DIM + col] = sum;
    }
  }
}

static void residual_cpu(const std::vector<float> &lhs,
                         const std::vector<float> &rhs,
                         std::vector<float> &output) {
  for (size_t i = 0; i < output.size(); ++i) output[i] = lhs[i] + rhs[i];
}

static void layernorm_cpu(const std::vector<float> &input,
                          const std::vector<float> &gamma,
                          const std::vector<float> &beta,
                          std::vector<float> &output) {
  for (int row = 0; row < TOKENS; ++row) {
    float mean = 0.0f;
    for (int col = 0; col < MODEL_DIM; ++col)
      mean += input[row * MODEL_DIM + col];
    mean /= MODEL_DIM;
    float var = 0.0f;
    for (int col = 0; col < MODEL_DIM; ++col) {
      float diff = input[row * MODEL_DIM + col] - mean;
      var += diff * diff;
    }
    var /= MODEL_DIM;
    float inv_std = 1.0f / std::sqrt(var + EPS);
    for (int col = 0; col < MODEL_DIM; ++col) {
      float norm = (input[row * MODEL_DIM + col] - mean) * inv_std;
      output[row * MODEL_DIM + col] = norm * gamma[col] + beta[col];
    }
  }
}

static void gelu_cpu(const std::vector<float> &input,
                     std::vector<float> &output) {
  for (size_t i = 0; i < input.size(); ++i) {
    float x = input[i];
    float cubic = x * x * x;
    float inner = 0.7978845608f * (x + 0.044715f * cubic);
    output[i] = 0.5f * x * (1.0f + std::tanh(inner));
  }
}

static bool compare_vector(const std::vector<float> &got,
                           const std::vector<float> &expected,
                           const char *name) {
  for (size_t i = 0; i < got.size(); ++i) {
    if (std::fabs(got[i] - expected[i]) > FP_ERROR) {
      std::cerr << RED << name << " mismatch at " << i << ": got " << got[i]
                << ", expected " << expected[i] << RESET << std::endl;
      return false;
    }
  }
  std::cout << GREEN << name << " passed" << RESET << std::endl;
  return true;
}

static std::vector<float> run_linear(
    cl::Context &context, cl::CommandQueue &queue, cl::Program &program,
    const std::vector<float> &input, const std::vector<float> &weights,
    const std::vector<float> &bias, int rows, int input_cols, int output_cols) {
  std::vector<float> output(rows * output_cols, 0.0f);
  cl::Buffer buf_input(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                       sizeof(float) * input.size(),
                       const_cast<float *>(input.data()));
  cl::Buffer buf_weights(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                         sizeof(float) * weights.size(),
                         const_cast<float *>(weights.data()));
  cl::Buffer buf_bias(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                      sizeof(float) * bias.size(),
                      const_cast<float *>(bias.data()));
  cl::Buffer buf_output(context, CL_MEM_WRITE_ONLY,
                        sizeof(float) * output.size());
  cl::Kernel kernel(program, "project_matrix");
  kernel.setArg(0, buf_input);
  kernel.setArg(1, buf_weights);
  kernel.setArg(2, buf_bias);
  kernel.setArg(3, buf_output);
  kernel.setArg(4, rows);
  kernel.setArg(5, input_cols);
  kernel.setArg(6, output_cols);
  queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(output.size()),
                             cl::NullRange);
  queue.enqueueReadBuffer(buf_output, CL_TRUE, 0, sizeof(float) * output.size(),
                          output.data());
  return output;
}

static std::vector<float> run_unary(cl::Context &context,
                                    cl::CommandQueue &queue,
                                    cl::Program &program,
                                    const std::vector<float> &input,
                                    const char *kernel_name) {
  std::vector<float> output(input.size(), 0.0f);
  cl::Buffer buf_input(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                       sizeof(float) * input.size(),
                       const_cast<float *>(input.data()));
  cl::Buffer buf_output(context, CL_MEM_WRITE_ONLY,
                        sizeof(float) * output.size());
  cl::Kernel kernel(program, kernel_name);
  kernel.setArg(0, buf_input);
  kernel.setArg(1, buf_output);
  kernel.setArg(2, static_cast<int>(input.size()));
  queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(output.size()),
                             cl::NullRange);
  queue.enqueueReadBuffer(buf_output, CL_TRUE, 0, sizeof(float) * output.size(),
                          output.data());
  return output;
}

static std::vector<float> run_residual(cl::Context &context,
                                       cl::CommandQueue &queue,
                                       cl::Program &program,
                                       const std::vector<float> &lhs,
                                       const std::vector<float> &rhs) {
  std::vector<float> output(lhs.size(), 0.0f);
  cl::Buffer buf_lhs(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                     sizeof(float) * lhs.size(),
                     const_cast<float *>(lhs.data()));
  cl::Buffer buf_rhs(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                     sizeof(float) * rhs.size(),
                     const_cast<float *>(rhs.data()));
  cl::Buffer buf_output(context, CL_MEM_WRITE_ONLY,
                        sizeof(float) * output.size());
  cl::Kernel kernel(program, "residual_add");
  kernel.setArg(0, buf_lhs);
  kernel.setArg(1, buf_rhs);
  kernel.setArg(2, buf_output);
  kernel.setArg(3, static_cast<int>(lhs.size()));
  queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(output.size()),
                             cl::NullRange);
  queue.enqueueReadBuffer(buf_output, CL_TRUE, 0, sizeof(float) * output.size(),
                          output.data());
  return output;
}

static std::vector<float> run_layernorm(cl::Context &context,
                                        cl::CommandQueue &queue,
                                        cl::Program &program,
                                        const std::vector<float> &input,
                                        const std::vector<float> &gamma,
                                        const std::vector<float> &beta) {
  std::vector<float> output(input.size(), 0.0f);
  cl::Buffer buf_input(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                       sizeof(float) * input.size(),
                       const_cast<float *>(input.data()));
  cl::Buffer buf_gamma(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                       sizeof(float) * gamma.size(),
                       const_cast<float *>(gamma.data()));
  cl::Buffer buf_beta(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                      sizeof(float) * beta.size(),
                      const_cast<float *>(beta.data()));
  cl::Buffer buf_output(context, CL_MEM_WRITE_ONLY,
                        sizeof(float) * output.size());
  cl::Kernel kernel(program, "layernorm");
  kernel.setArg(0, buf_input);
  kernel.setArg(1, buf_gamma);
  kernel.setArg(2, buf_beta);
  kernel.setArg(3, buf_output);
  kernel.setArg(4, MODEL_DIM);
  kernel.setArg(5, EPS);
  queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(TOKENS),
                             cl::NullRange);
  queue.enqueueReadBuffer(buf_output, CL_TRUE, 0, sizeof(float) * output.size(),
                          output.data());
  return output;
}

static std::vector<float> run_scores(cl::Context &context,
                                     cl::CommandQueue &queue,
                                     cl::Program &program,
                                     const std::vector<float> &query,
                                     const std::vector<float> &key) {
  std::vector<float> output(TOKENS * TOKENS, 0.0f);
  cl::Buffer buf_q(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                   sizeof(float) * query.size(),
                   const_cast<float *>(query.data()));
  cl::Buffer buf_k(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                   sizeof(float) * key.size(), const_cast<float *>(key.data()));
  cl::Buffer buf_output(context, CL_MEM_WRITE_ONLY,
                        sizeof(float) * output.size());
  cl::Kernel kernel(program, "attention_scores");
  kernel.setArg(0, buf_q);
  kernel.setArg(1, buf_k);
  kernel.setArg(2, buf_output);
  kernel.setArg(3, TOKENS);
  kernel.setArg(4, HEAD_DIM);
  kernel.setArg(5, 1.0f / std::sqrt(static_cast<float>(HEAD_DIM)));
  queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(output.size()),
                             cl::NullRange);
  queue.enqueueReadBuffer(buf_output, CL_TRUE, 0, sizeof(float) * output.size(),
                          output.data());
  return output;
}

static std::vector<float> run_softmax(cl::Context &context,
                                      cl::CommandQueue &queue,
                                      cl::Program &program,
                                      const std::vector<float> &scores) {
  std::vector<float> output(scores.size(), 0.0f);
  cl::Buffer buf_scores(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                        sizeof(float) * scores.size(),
                        const_cast<float *>(scores.data()));
  cl::Buffer buf_output(context, CL_MEM_WRITE_ONLY,
                        sizeof(float) * output.size());
  cl::Kernel kernel(program, "attention_softmax");
  kernel.setArg(0, buf_scores);
  kernel.setArg(1, buf_output);
  kernel.setArg(2, TOKENS);
  queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(TOKENS),
                             cl::NullRange);
  queue.enqueueReadBuffer(buf_output, CL_TRUE, 0, sizeof(float) * output.size(),
                          output.data());
  return output;
}

static std::vector<float> run_context(cl::Context &context,
                                      cl::CommandQueue &queue,
                                      cl::Program &program,
                                      const std::vector<float> &probs,
                                      const std::vector<float> &value) {
  std::vector<float> output(TOKENS * HEAD_DIM, 0.0f);
  cl::Buffer buf_probs(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                       sizeof(float) * probs.size(),
                       const_cast<float *>(probs.data()));
  cl::Buffer buf_value(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                       sizeof(float) * value.size(),
                       const_cast<float *>(value.data()));
  cl::Buffer buf_output(context, CL_MEM_WRITE_ONLY,
                        sizeof(float) * output.size());
  cl::Kernel kernel(program, "attention_context");
  kernel.setArg(0, buf_probs);
  kernel.setArg(1, buf_value);
  kernel.setArg(2, buf_output);
  kernel.setArg(3, TOKENS);
  kernel.setArg(4, HEAD_DIM);
  queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(output.size()),
                             cl::NullRange);
  queue.enqueueReadBuffer(buf_output, CL_TRUE, 0, sizeof(float) * output.size(),
                          output.data());
  return output;
}

int main() {
  srand(11);
  std::vector<cl::Platform> platforms;
  cl::Platform::get(&platforms);
  if (platforms.empty()) return -1;
  std::vector<cl::Device> devices;
  platforms[0].getDevices(CL_DEVICE_TYPE_GPU, &devices);
  if (devices.empty()) return -1;
  cl::Context context({devices[0]});
  cl::CommandQueue queue(context, devices[0]);
  std::string kernel_path = std::string(KERNEL_PATH);
  std::cout << "Kernel Path : " << kernel_path << std::endl;
  std::ifstream t(kernel_path);
  if (!t) {
    std::cerr << "Error Opening Kernel Source file\n";
    return -1;
  }
  std::string source = {std::istreambuf_iterator<char>(t),
                        std::istreambuf_iterator<char>()};
  cl::Program program(context, source);
  if (program.build() != CL_SUCCESS) return -1;

  std::vector<float> input(TOKENS * MODEL_DIM);
  std::vector<float> wq(MODEL_DIM * HEAD_DIM), bq(HEAD_DIM);
  std::vector<float> wk(MODEL_DIM * HEAD_DIM), bk(HEAD_DIM);
  std::vector<float> wv(MODEL_DIM * HEAD_DIM), bv(HEAD_DIM);
  std::vector<float> wo(HEAD_DIM * MODEL_DIM), bo(MODEL_DIM);
  std::vector<float> gamma(MODEL_DIM), beta(MODEL_DIM);
  std::vector<float> w1(MODEL_DIM * HIDDEN_DIM), b1(HIDDEN_DIM);
  std::vector<float> w2(HIDDEN_DIM * MODEL_DIM), b2(MODEL_DIM);

  auto fill = [](std::vector<float> &vec, float scale) {
    for (float &x : vec) x = static_cast<float>((rand() % 200) - 100) / scale;
  };
  fill(input, 20.0f);
  fill(wq, 100.0f);
  fill(bq, 100.0f);
  fill(wk, 100.0f);
  fill(bk, 100.0f);
  fill(wv, 100.0f);
  fill(bv, 100.0f);
  fill(wo, 100.0f);
  fill(bo, 100.0f);
  fill(gamma, 100.0f);
  fill(beta, 100.0f);
  fill(w1, 100.0f);
  fill(b1, 100.0f);
  fill(w2, 100.0f);
  fill(b2, 100.0f);
  for (float &x : gamma) x = std::fabs(x) + 0.1f;

  std::vector<float> q_ref(TOKENS * HEAD_DIM, 0.0f);
  std::vector<float> k_ref(TOKENS * HEAD_DIM, 0.0f);
  std::vector<float> v_ref(TOKENS * HEAD_DIM, 0.0f);
  std::vector<float> scores_ref(TOKENS * TOKENS, 0.0f);
  std::vector<float> probs_ref(TOKENS * TOKENS, 0.0f);
  std::vector<float> context_ref(TOKENS * HEAD_DIM, 0.0f);
  std::vector<float> attn_proj_ref(TOKENS * MODEL_DIM, 0.0f);
  std::vector<float> residual1_ref(TOKENS * MODEL_DIM, 0.0f);
  std::vector<float> norm_ref(TOKENS * MODEL_DIM, 0.0f);
  std::vector<float> ff1_ref(TOKENS * HIDDEN_DIM, 0.0f);
  std::vector<float> gelu_ref(TOKENS * HIDDEN_DIM, 0.0f);
  std::vector<float> ff2_ref(TOKENS * MODEL_DIM, 0.0f);
  std::vector<float> output_ref(TOKENS * MODEL_DIM, 0.0f);
  linear_cpu(input, wq, bq, q_ref, TOKENS, MODEL_DIM, HEAD_DIM);
  linear_cpu(input, wk, bk, k_ref, TOKENS, MODEL_DIM, HEAD_DIM);
  linear_cpu(input, wv, bv, v_ref, TOKENS, MODEL_DIM, HEAD_DIM);
  attention_scores_cpu(q_ref, k_ref, scores_ref);
  softmax_cpu(scores_ref, probs_ref);
  context_cpu(probs_ref, v_ref, context_ref);
  linear_cpu(context_ref, wo, bo, attn_proj_ref, TOKENS, HEAD_DIM, MODEL_DIM);
  residual_cpu(input, attn_proj_ref, residual1_ref);
  layernorm_cpu(residual1_ref, gamma, beta, norm_ref);
  linear_cpu(norm_ref, w1, b1, ff1_ref, TOKENS, MODEL_DIM, HIDDEN_DIM);
  gelu_cpu(ff1_ref, gelu_ref);
  linear_cpu(gelu_ref, w2, b2, ff2_ref, TOKENS, HIDDEN_DIM, MODEL_DIM);
  residual_cpu(residual1_ref, ff2_ref, output_ref);

  std::vector<float> q_out = run_linear(context, queue, program, input, wq, bq,
                                        TOKENS, MODEL_DIM, HEAD_DIM);
  std::vector<float> k_out = run_linear(context, queue, program, input, wk, bk,
                                        TOKENS, MODEL_DIM, HEAD_DIM);
  std::vector<float> v_out = run_linear(context, queue, program, input, wv, bv,
                                        TOKENS, MODEL_DIM, HEAD_DIM);
  std::vector<float> scores_out =
      run_scores(context, queue, program, q_out, k_out);
  std::vector<float> probs_out =
      run_softmax(context, queue, program, scores_out);
  std::vector<float> context_out =
      run_context(context, queue, program, probs_out, v_out);
  std::vector<float> attn_proj_out =
      run_linear(context, queue, program, context_out, wo, bo, TOKENS, HEAD_DIM,
                 MODEL_DIM);
  std::vector<float> residual1_out =
      run_residual(context, queue, program, input, attn_proj_out);
  std::vector<float> norm_out =
      run_layernorm(context, queue, program, residual1_out, gamma, beta);
  std::vector<float> ff1_out = run_linear(context, queue, program, norm_out, w1,
                                          b1, TOKENS, MODEL_DIM, HIDDEN_DIM);
  std::vector<float> gelu_out =
      run_unary(context, queue, program, ff1_out, "gelu_activation");
  std::vector<float> ff2_out = run_linear(context, queue, program, gelu_out, w2,
                                          b2, TOKENS, HIDDEN_DIM, MODEL_DIM);
  std::vector<float> output_out =
      run_residual(context, queue, program, residual1_out, ff2_out);

  bool passed = true;
  passed &= compare_vector(q_out, q_ref, "transformer_q");
  passed &= compare_vector(k_out, k_ref, "transformer_k");
  passed &= compare_vector(v_out, v_ref, "transformer_v");
  passed &= compare_vector(scores_out, scores_ref, "transformer_scores");
  passed &= compare_vector(probs_out, probs_ref, "transformer_softmax");
  passed &= compare_vector(context_out, context_ref, "transformer_context");
  passed &=
      compare_vector(attn_proj_out, attn_proj_ref, "transformer_attn_proj");
  passed &=
      compare_vector(residual1_out, residual1_ref, "transformer_residual1");
  passed &= compare_vector(norm_out, norm_ref, "transformer_norm");
  passed &= compare_vector(ff1_out, ff1_ref, "transformer_ff1");
  passed &= compare_vector(gelu_out, gelu_ref, "transformer_gelu");
  passed &= compare_vector(ff2_out, ff2_ref, "transformer_ff2");
  passed &= compare_vector(output_out, output_ref, "transformer_output");
  return passed ? 0 : -1;
}
