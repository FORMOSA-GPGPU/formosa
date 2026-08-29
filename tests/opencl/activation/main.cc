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
#include <functional>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#ifndef KERNEL_PATH
#define KERNEL_PATH "./kernel.cl"
#endif

#define ELEMENTS 256
#define FP_ERROR 0.001f

#define RED "\033[0;31m"
#define GREEN "\033[0;32m"
#define RESET "\033[0m"

static float gelu_cpu(float x) {
  float cubic = x * x * x;
  float inner = 0.7978845608f * (x + 0.044715f * cubic);
  return 0.5f * x * (1.0f + std::tanh(inner));
}

static bool close_enough(float a, float b) {
  return std::fabs(a - b) < FP_ERROR;
}

static bool run_unary_test(cl::Context &context, cl::CommandQueue &queue,
                           cl::Program &program,
                           const std::vector<float> &input,
                           const std::vector<float> &expected,
                           const char *kernel_name, float alpha = 0.0f) {
  cl::Buffer buf_input(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                       sizeof(float) * input.size(),
                       const_cast<float *>(input.data()));
  cl::Buffer buf_output(context, CL_MEM_WRITE_ONLY,
                        sizeof(float) * input.size());

  cl::Kernel kernel(program, kernel_name);
  kernel.setArg(0, buf_input);
  kernel.setArg(1, buf_output);
  if (std::string(kernel_name) == "leaky_relu_kernel") {
    kernel.setArg(2, alpha);
  }

  std::vector<float> output(input.size(), 0.0f);
  queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(input.size()),
                             cl::NullRange);
  queue.enqueueReadBuffer(buf_output, CL_TRUE, 0, sizeof(float) * output.size(),
                          output.data());

  bool has_error = false;
  for (size_t i = 0; i < output.size(); ++i) {
    if (!close_enough(output[i], expected[i])) {
      std::cerr << RED << kernel_name << " mismatch at " << i << ": got "
                << output[i] << ", expected " << expected[i] << RESET
                << std::endl;
      has_error = true;
      break;
    }
  }

  if (!has_error) {
    std::cout << GREEN << kernel_name << " passed" << RESET << std::endl;
  }

  return !has_error;
}

int main() {
  srand(0);

  std::vector<cl::Platform> platforms;
  cl::Platform::get(&platforms);
  if (platforms.empty()) {
    std::cerr << "No platforms!" << std::endl;
    return -1;
  }

  std::vector<cl::Device> devices;
  platforms[0].getDevices(CL_DEVICE_TYPE_GPU, &devices);
  if (devices.empty()) {
    std::cerr << "No Devices!" << std::endl;
    return -1;
  }

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

  std::vector<float> input(ELEMENTS);
  for (float &x : input) {
    x = static_cast<float>((rand() % 200) - 100) / 25.0f;
  }

  std::vector<float> relu_expected(ELEMENTS);
  std::vector<float> leaky_relu_expected(ELEMENTS);
  std::vector<float> sigmoid_expected(ELEMENTS);
  std::vector<float> tanh_expected(ELEMENTS);
  std::vector<float> gelu_expected(ELEMENTS);
  constexpr float kAlpha = 0.1f;

  for (size_t i = 0; i < input.size(); ++i) {
    relu_expected[i] = std::max(0.0f, input[i]);
    leaky_relu_expected[i] = input[i] > 0.0f ? input[i] : kAlpha * input[i];
    sigmoid_expected[i] = 1.0f / (1.0f + std::exp(-input[i]));
    tanh_expected[i] = std::tanh(input[i]);
    gelu_expected[i] = gelu_cpu(input[i]);
  }

  bool passed = true;
  passed &= run_unary_test(context, queue, program, input, relu_expected,
                           "relu_kernel");
  passed &= run_unary_test(context, queue, program, input, leaky_relu_expected,
                           "leaky_relu_kernel", kAlpha);
  passed &= run_unary_test(context, queue, program, input, sigmoid_expected,
                           "sigmoid_kernel");
  passed &= run_unary_test(context, queue, program, input, tanh_expected,
                           "tanh_kernel");
  passed &= run_unary_test(context, queue, program, input, gelu_expected,
                           "gelu_kernel");
  return passed ? 0 : -1;
}
