// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <CL/opencl.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#ifndef KERNEL_PATH
#define KERNEL_PATH "./kernel.cl"
#endif

#define TOKENS 4
#define MODEL_DIM 8
#define HEAD_DIM 8
#define FP_ERROR 0.001f

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
      for (int k = 0; k < input_cols; ++k) {
        sum += input[row * input_cols + k] * weights[k * output_cols + col];
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

static void softmax_rows_cpu(const std::vector<float> &scores,
                             std::vector<float> &probs) {
  for (int row = 0; row < TOKENS; ++row) {
    float max_val = scores[row * TOKENS];
    for (int col = 1; col < TOKENS; ++col) {
      max_val = std::max(max_val, scores[row * TOKENS + col]);
    }

    float sum = 0.0f;
    for (int col = 0; col < TOKENS; ++col) {
      float value = std::exp(scores[row * TOKENS + col] - max_val);
      probs[row * TOKENS + col] = value;
      sum += value;
    }

    for (int col = 0; col < TOKENS; ++col) {
      probs[row * TOKENS + col] /= sum;
    }
  }
}

static void attention_context_cpu(const std::vector<float> &probs,
                                  const std::vector<float> &value,
                                  std::vector<float> &context) {
  for (int row = 0; row < TOKENS; ++row) {
    for (int col = 0; col < HEAD_DIM; ++col) {
      float sum = 0.0f;
      for (int token = 0; token < TOKENS; ++token) {
        sum += probs[row * TOKENS + token] * value[token * HEAD_DIM + col];
      }
      context[row * HEAD_DIM + col] = sum;
    }
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

static std::vector<float> run_linear_kernel(
    cl::Context &context, cl::CommandQueue &queue, cl::Program &program,
    const std::vector<float> &input, const std::vector<float> &weights,
    const std::vector<float> &bias, const char *kernel_name, int rows,
    int input_cols, int output_cols) {
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

  cl::Kernel kernel(program, kernel_name);
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

int main() {
  srand(7);

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
  if (program.build() != CL_SUCCESS) {
    std::cerr << "Fail to build" << std::endl;
    return -1;
  }

  std::vector<float> input(TOKENS * MODEL_DIM);
  std::vector<float> wq(MODEL_DIM * HEAD_DIM);
  std::vector<float> bq(HEAD_DIM);
  std::vector<float> wk(MODEL_DIM * HEAD_DIM);
  std::vector<float> bk(HEAD_DIM);
  std::vector<float> wv(MODEL_DIM * HEAD_DIM);
  std::vector<float> bv(HEAD_DIM);
  std::vector<float> wo(HEAD_DIM * MODEL_DIM);
  std::vector<float> bo(MODEL_DIM);

  for (float &x : input) x = static_cast<float>((rand() % 200) - 100) / 20.0f;
  for (float &x : wq) x = static_cast<float>((rand() % 200) - 100) / 100.0f;
  for (float &x : bq) x = static_cast<float>((rand() % 200) - 100) / 100.0f;
  for (float &x : wk) x = static_cast<float>((rand() % 200) - 100) / 100.0f;
  for (float &x : bk) x = static_cast<float>((rand() % 200) - 100) / 100.0f;
  for (float &x : wv) x = static_cast<float>((rand() % 200) - 100) / 100.0f;
  for (float &x : bv) x = static_cast<float>((rand() % 200) - 100) / 100.0f;
  for (float &x : wo) x = static_cast<float>((rand() % 200) - 100) / 100.0f;
  for (float &x : bo) x = static_cast<float>((rand() % 200) - 100) / 100.0f;

  std::vector<float> q_ref(TOKENS * HEAD_DIM, 0.0f);
  std::vector<float> k_ref(TOKENS * HEAD_DIM, 0.0f);
  std::vector<float> v_ref(TOKENS * HEAD_DIM, 0.0f);
  std::vector<float> scores_ref(TOKENS * TOKENS, 0.0f);
  std::vector<float> probs_ref(TOKENS * TOKENS, 0.0f);
  std::vector<float> context_ref(TOKENS * HEAD_DIM, 0.0f);
  std::vector<float> output_ref(TOKENS * MODEL_DIM, 0.0f);

  linear_cpu(input, wq, bq, q_ref, TOKENS, MODEL_DIM, HEAD_DIM);
  linear_cpu(input, wk, bk, k_ref, TOKENS, MODEL_DIM, HEAD_DIM);
  linear_cpu(input, wv, bv, v_ref, TOKENS, MODEL_DIM, HEAD_DIM);
  attention_scores_cpu(q_ref, k_ref, scores_ref);
  softmax_rows_cpu(scores_ref, probs_ref);
  attention_context_cpu(probs_ref, v_ref, context_ref);
  linear_cpu(context_ref, wo, bo, output_ref, TOKENS, HEAD_DIM, MODEL_DIM);

  std::vector<float> q_out =
      run_linear_kernel(context, queue, program, input, wq, bq,
                        "project_matrix", TOKENS, MODEL_DIM, HEAD_DIM);
  std::vector<float> k_out =
      run_linear_kernel(context, queue, program, input, wk, bk,
                        "project_matrix", TOKENS, MODEL_DIM, HEAD_DIM);
  std::vector<float> v_out =
      run_linear_kernel(context, queue, program, input, wv, bv,
                        "project_matrix", TOKENS, MODEL_DIM, HEAD_DIM);

  std::vector<float> scores_out(TOKENS * TOKENS, 0.0f);
  std::vector<float> probs_out(TOKENS * TOKENS, 0.0f);
  std::vector<float> context_out(TOKENS * HEAD_DIM, 0.0f);
  std::vector<float> output_out(TOKENS * MODEL_DIM, 0.0f);

  cl::Buffer buf_q(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                   sizeof(float) * q_out.size(), q_out.data());
  cl::Buffer buf_k(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                   sizeof(float) * k_out.size(), k_out.data());
  cl::Buffer buf_v(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                   sizeof(float) * v_out.size(), v_out.data());
  cl::Buffer buf_wo(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                    sizeof(float) * wo.size(), wo.data());
  cl::Buffer buf_bo(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                    sizeof(float) * bo.size(), bo.data());
  cl::Buffer buf_scores(context, CL_MEM_READ_WRITE,
                        sizeof(float) * scores_out.size());
  cl::Buffer buf_probs(context, CL_MEM_READ_WRITE,
                       sizeof(float) * probs_out.size());
  cl::Buffer buf_context(context, CL_MEM_READ_WRITE,
                         sizeof(float) * context_out.size());
  cl::Buffer buf_output(context, CL_MEM_WRITE_ONLY,
                        sizeof(float) * output_out.size());

  const float scale = 1.0f / std::sqrt(static_cast<float>(HEAD_DIM));
  cl::Kernel score_kernel(program, "attention_scores");
  score_kernel.setArg(0, buf_q);
  score_kernel.setArg(1, buf_k);
  score_kernel.setArg(2, buf_scores);
  score_kernel.setArg(3, TOKENS);
  score_kernel.setArg(4, HEAD_DIM);
  score_kernel.setArg(5, scale);
  queue.enqueueNDRangeKernel(score_kernel, cl::NullRange,
                             cl::NDRange(scores_out.size()), cl::NullRange);
  queue.enqueueReadBuffer(buf_scores, CL_TRUE, 0,
                          sizeof(float) * scores_out.size(), scores_out.data());

  cl::Kernel softmax_kernel(program, "attention_softmax");
  softmax_kernel.setArg(0, buf_scores);
  softmax_kernel.setArg(1, buf_probs);
  softmax_kernel.setArg(2, TOKENS);
  queue.enqueueNDRangeKernel(softmax_kernel, cl::NullRange, cl::NDRange(TOKENS),
                             cl::NullRange);
  queue.enqueueReadBuffer(buf_probs, CL_TRUE, 0,
                          sizeof(float) * probs_out.size(), probs_out.data());

  cl::Kernel context_kernel(program, "attention_context");
  context_kernel.setArg(0, buf_probs);
  context_kernel.setArg(1, buf_v);
  context_kernel.setArg(2, buf_context);
  context_kernel.setArg(3, TOKENS);
  context_kernel.setArg(4, HEAD_DIM);
  queue.enqueueNDRangeKernel(context_kernel, cl::NullRange,
                             cl::NDRange(context_out.size()), cl::NullRange);
  queue.enqueueReadBuffer(buf_context, CL_TRUE, 0,
                          sizeof(float) * context_out.size(),
                          context_out.data());

  cl::Kernel project_o(program, "project_output");
  project_o.setArg(0, buf_context);
  project_o.setArg(1, buf_wo);
  project_o.setArg(2, buf_bo);
  project_o.setArg(3, buf_output);
  project_o.setArg(4, TOKENS);
  project_o.setArg(5, HEAD_DIM);
  project_o.setArg(6, MODEL_DIM);
  queue.enqueueNDRangeKernel(project_o, cl::NullRange,
                             cl::NDRange(output_out.size()), cl::NullRange);
  queue.enqueueReadBuffer(buf_output, CL_TRUE, 0,
                          sizeof(float) * output_out.size(), output_out.data());

  bool passed = true;
  passed &= compare_vector(q_out, q_ref, "query_projection");
  passed &= compare_vector(k_out, k_ref, "key_projection");
  passed &= compare_vector(v_out, v_ref, "value_projection");
  passed &= compare_vector(scores_out, scores_ref, "attention_scores");
  passed &= compare_vector(probs_out, probs_ref, "attention_softmax");
  passed &= compare_vector(context_out, context_ref, "attention_context");
  passed &= compare_vector(output_out, output_ref, "attention_output");

  return passed ? 0 : -1;
}
