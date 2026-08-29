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

#define ROWS 16
#define COLS 32
#define FP_ERROR 0.0001f

#define RED "\033[0;31m"
#define GREEN "\033[0;32m"
#define RESET "\033[0m"

int main() {
  srand(1);

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

  std::vector<float> input(ROWS * COLS);
  std::vector<float> bias(COLS);
  std::vector<float> output(ROWS * COLS, 0.0f);
  std::vector<float> expected(ROWS * COLS, 0.0f);

  for (float &x : input) x = static_cast<float>((rand() % 200) - 100) / 20.0f;
  for (float &x : bias) x = static_cast<float>((rand() % 100) - 50) / 50.0f;

  for (int row = 0; row < ROWS; ++row) {
    for (int col = 0; col < COLS; ++col) {
      float sum = input[row * COLS + col] + bias[col];
      expected[row * COLS + col] = std::max(0.0f, sum);
    }
  }

  cl::Buffer buf_input(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                       sizeof(float) * input.size(), input.data());
  cl::Buffer buf_bias(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                      sizeof(float) * bias.size(), bias.data());
  cl::Buffer buf_output(context, CL_MEM_WRITE_ONLY,
                        sizeof(float) * output.size());

  cl::Kernel kernel(program, "bias_relu");
  kernel.setArg(0, buf_input);
  kernel.setArg(1, buf_bias);
  kernel.setArg(2, buf_output);
  kernel.setArg(3, COLS);

  queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(output.size()),
                             cl::NullRange);
  queue.enqueueReadBuffer(buf_output, CL_TRUE, 0, sizeof(float) * output.size(),
                          output.data());

  bool has_error = false;
  for (size_t i = 0; i < output.size(); ++i) {
    if (std::fabs(output[i] - expected[i]) > FP_ERROR) {
      std::cerr << RED << "bias_relu mismatch at " << i << ": got " << output[i]
                << ", expected " << expected[i] << RESET << std::endl;
      has_error = true;
      break;
    }
  }

  if (has_error) return -1;
  std::cout << GREEN << "bias_relu passed" << RESET << std::endl;
  return 0;
}
