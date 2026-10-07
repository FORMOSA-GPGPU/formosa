// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <CL/opencl.hpp>
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

static void silu_cpu(const std::vector<float> &input,
                     std::vector<float> &output) {
  for (size_t i = 0; i < input.size(); ++i) {
    float x = input[i];
    output[i] = x / (1.0f + std::exp(-x));
  }
}

static void mul_cpu(const std::vector<float> &lhs,
                    const std::vector<float> &rhs, std::vector<float> &output) {
  for (size_t i = 0; i < output.size(); ++i) output[i] = lhs[i] * rhs[i];
}

static void add_cpu(const std::vector<float> &lhs,
                    const std::vector<float> &rhs, std::vector<float> &output) {
  for (size_t i = 0; i < output.size(); ++i) output[i] = lhs[i] + rhs[i];
}

static void rmsnorm_cpu(const std::vector<float> &input,
                        const std::vector<float> &gamma,
                        std::vector<float> &output) {
  for (int row = 0; row < TOKENS; ++row) {
    float mean_sq = 0.0f;
    for (int col = 0; col < MODEL_DIM; ++col) {
      float x = input[row * MODEL_DIM + col];
      mean_sq += x * x;
    }
    mean_sq /= static_cast<float>(MODEL_DIM);
    float inv_rms = 1.0f / std::sqrt(mean_sq + EPS);
    for (int col = 0; col < MODEL_DIM; ++col) {
      output[row * MODEL_DIM + col] =
          input[row * MODEL_DIM + col] * inv_rms * gamma[col];
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

static std::vector<float> run_binary(cl::Context &context,
                                     cl::CommandQueue &queue,
                                     cl::Program &program,
                                     const std::vector<float> &lhs,
                                     const std::vector<float> &rhs,
                                     const char *kernel_name) {
  std::vector<float> output(lhs.size(), 0.0f);
  cl::Buffer buf_lhs(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                     sizeof(float) * lhs.size(),
                     const_cast<float *>(lhs.data()));
  cl::Buffer buf_rhs(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                     sizeof(float) * rhs.size(),
                     const_cast<float *>(rhs.data()));
  cl::Buffer buf_output(context, CL_MEM_WRITE_ONLY,
                        sizeof(float) * output.size());
  cl::Kernel kernel(program, kernel_name);
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

static std::vector<float> run_rmsnorm(cl::Context &context,
                                      cl::CommandQueue &queue,
                                      cl::Program &program,
                                      const std::vector<float> &input,
                                      const std::vector<float> &gamma) {
  std::vector<float> output(input.size(), 0.0f);
  cl::Buffer buf_input(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                       sizeof(float) * input.size(),
                       const_cast<float *>(input.data()));
  cl::Buffer buf_gamma(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                       sizeof(float) * gamma.size(),
                       const_cast<float *>(gamma.data()));
  cl::Buffer buf_output(context, CL_MEM_WRITE_ONLY,
                        sizeof(float) * output.size());
  cl::Kernel kernel(program, "rmsnorm");
  kernel.setArg(0, buf_input);
  kernel.setArg(1, buf_gamma);
  kernel.setArg(2, buf_output);
  kernel.setArg(3, MODEL_DIM);
  kernel.setArg(4, EPS);
  queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(TOKENS),
                             cl::NullRange);
  queue.enqueueReadBuffer(buf_output, CL_TRUE, 0, sizeof(float) * output.size(),
                          output.data());
  return output;
}

int main() {
  srand(13);
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
  std::vector<float> wg(MODEL_DIM * HIDDEN_DIM);
  std::vector<float> bg(HIDDEN_DIM);
  std::vector<float> wu(MODEL_DIM * HIDDEN_DIM);
  std::vector<float> bu(HIDDEN_DIM);
  std::vector<float> wd(HIDDEN_DIM * MODEL_DIM);
  std::vector<float> bd(MODEL_DIM);
  std::vector<float> gamma(MODEL_DIM);
  for (float &x : input) x = static_cast<float>((rand() % 200) - 100) / 20.0f;
  for (float &x : wg) x = static_cast<float>((rand() % 200) - 100) / 100.0f;
  for (float &x : bg) x = static_cast<float>((rand() % 200) - 100) / 100.0f;
  for (float &x : wu) x = static_cast<float>((rand() % 200) - 100) / 100.0f;
  for (float &x : bu) x = static_cast<float>((rand() % 200) - 100) / 100.0f;
  for (float &x : wd) x = static_cast<float>((rand() % 200) - 100) / 100.0f;
  for (float &x : bd) x = static_cast<float>((rand() % 200) - 100) / 100.0f;
  for (float &x : gamma) x = static_cast<float>((rand() % 100) + 1) / 100.0f;

  std::vector<float> gate_ref(TOKENS * HIDDEN_DIM, 0.0f);
  std::vector<float> silu_ref(TOKENS * HIDDEN_DIM, 0.0f);
  std::vector<float> up_ref(TOKENS * HIDDEN_DIM, 0.0f);
  std::vector<float> gated_ref(TOKENS * HIDDEN_DIM, 0.0f);
  std::vector<float> down_ref(TOKENS * MODEL_DIM, 0.0f);
  std::vector<float> residual_ref(TOKENS * MODEL_DIM, 0.0f);
  std::vector<float> output_ref(TOKENS * MODEL_DIM, 0.0f);

  linear_cpu(input, wg, bg, gate_ref, TOKENS, MODEL_DIM, HIDDEN_DIM);
  silu_cpu(gate_ref, silu_ref);
  linear_cpu(input, wu, bu, up_ref, TOKENS, MODEL_DIM, HIDDEN_DIM);
  mul_cpu(silu_ref, up_ref, gated_ref);
  linear_cpu(gated_ref, wd, bd, down_ref, TOKENS, HIDDEN_DIM, MODEL_DIM);
  add_cpu(input, down_ref, residual_ref);
  rmsnorm_cpu(residual_ref, gamma, output_ref);

  std::vector<float> gate_out = run_linear(context, queue, program, input, wg,
                                           bg, TOKENS, MODEL_DIM, HIDDEN_DIM);
  std::vector<float> silu_out =
      run_unary(context, queue, program, gate_out, "silu_activation");
  std::vector<float> up_out = run_linear(context, queue, program, input, wu, bu,
                                         TOKENS, MODEL_DIM, HIDDEN_DIM);
  std::vector<float> gated_out =
      run_binary(context, queue, program, silu_out, up_out, "elementwise_mul");
  std::vector<float> down_out =
      run_linear(context, queue, program, gated_out, wd, bd, TOKENS, HIDDEN_DIM,
                 MODEL_DIM);
  std::vector<float> residual_out =
      run_binary(context, queue, program, input, down_out, "residual_add");

  std::vector<float> output_out =
      run_rmsnorm(context, queue, program, residual_out, gamma);

  bool passed = true;
  passed &= compare_vector(gate_out, gate_ref, "swiglu_rmsnorm_gate");
  passed &= compare_vector(silu_out, silu_ref, "swiglu_rmsnorm_silu");
  passed &= compare_vector(up_out, up_ref, "swiglu_rmsnorm_up");
  passed &= compare_vector(gated_out, gated_ref, "swiglu_rmsnorm_gated");
  passed &= compare_vector(down_out, down_ref, "swiglu_rmsnorm_down");
  passed &=
      compare_vector(residual_out, residual_ref, "swiglu_rmsnorm_residual");
  passed &= compare_vector(output_out, output_ref, "swiglu_rmsnorm_output");
  return passed ? 0 : -1;
}
