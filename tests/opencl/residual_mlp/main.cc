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

#define TOKENS 4
#define MODEL_DIM 8
#define HIDDEN_DIM 16
#define EPS 1e-5f
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
      for (int i = 0; i < input_cols; ++i) {
        sum += input[row * input_cols + i] * weights[i * output_cols + col];
      }
      output[row * output_cols + col] = sum;
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

int main() {
  srand(8);
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
  std::vector<float> w1(MODEL_DIM * HIDDEN_DIM);
  std::vector<float> b1(HIDDEN_DIM);
  std::vector<float> w2(HIDDEN_DIM * MODEL_DIM);
  std::vector<float> b2(MODEL_DIM);
  std::vector<float> gamma(MODEL_DIM);
  std::vector<float> beta(MODEL_DIM);
  for (float &x : input) x = static_cast<float>((rand() % 200) - 100) / 20.0f;
  for (float &x : w1) x = static_cast<float>((rand() % 200) - 100) / 100.0f;
  for (float &x : b1) x = static_cast<float>((rand() % 200) - 100) / 100.0f;
  for (float &x : w2) x = static_cast<float>((rand() % 200) - 100) / 100.0f;
  for (float &x : b2) x = static_cast<float>((rand() % 200) - 100) / 100.0f;
  for (float &x : gamma) x = static_cast<float>((rand() % 100) + 1) / 100.0f;
  for (float &x : beta) x = static_cast<float>((rand() % 200) - 100) / 100.0f;

  std::vector<float> fc1_ref(TOKENS * HIDDEN_DIM, 0.0f);
  std::vector<float> act_ref(TOKENS * HIDDEN_DIM, 0.0f);
  std::vector<float> fc2_ref(TOKENS * MODEL_DIM, 0.0f);
  std::vector<float> residual_ref(TOKENS * MODEL_DIM, 0.0f);
  std::vector<float> output_ref(TOKENS * MODEL_DIM, 0.0f);
  linear_cpu(input, w1, b1, fc1_ref, TOKENS, MODEL_DIM, HIDDEN_DIM);
  gelu_cpu(fc1_ref, act_ref);
  linear_cpu(act_ref, w2, b2, fc2_ref, TOKENS, HIDDEN_DIM, MODEL_DIM);
  residual_cpu(input, fc2_ref, residual_ref);
  layernorm_cpu(residual_ref, gamma, beta, output_ref);

  std::vector<float> fc1_out = run_linear(context, queue, program, input, w1,
                                          b1, TOKENS, MODEL_DIM, HIDDEN_DIM);
  std::vector<float> act_out =
      run_unary(context, queue, program, fc1_out, "gelu_activation");
  std::vector<float> fc2_out = run_linear(context, queue, program, act_out, w2,
                                          b2, TOKENS, HIDDEN_DIM, MODEL_DIM);
  std::vector<float> residual_out =
      run_residual(context, queue, program, input, fc2_out);
  std::vector<float> output_out =
      run_layernorm(context, queue, program, residual_out, gamma, beta);

  bool passed = true;
  passed &= compare_vector(fc1_out, fc1_ref, "residual_mlp_fc1");
  passed &= compare_vector(act_out, act_ref, "residual_mlp_gelu");
  passed &= compare_vector(fc2_out, fc2_ref, "residual_mlp_fc2");
  passed &= compare_vector(residual_out, residual_ref, "residual_mlp_residual");
  passed &= compare_vector(output_out, output_ref, "residual_mlp_output");
  return passed ? 0 : -1;
}
