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

#define ROWS 8
#define COLS 16
#define FP_ERROR 0.0005f

#define RED "\033[0;31m"
#define GREEN "\033[0;32m"
#define RESET "\033[0m"

static void softmax_cpu(const std::vector<float> &input,
                        std::vector<float> &output) {
  for (int row = 0; row < ROWS; ++row) {
    float max_val = input[row * COLS];
    for (int col = 1; col < COLS; ++col) {
      max_val = std::max(max_val, input[row * COLS + col]);
    }
    float sum = 0.0f;
    for (int col = 0; col < COLS; ++col) {
      float value = std::exp(input[row * COLS + col] - max_val);
      output[row * COLS + col] = value;
      sum += value;
    }
    for (int col = 0; col < COLS; ++col) {
      output[row * COLS + col] /= sum;
    }
  }
}

int main() {
  srand(4);

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
  std::vector<float> output(ROWS * COLS, 0.0f);
  std::vector<float> expected(ROWS * COLS, 0.0f);
  for (float &x : input) x = static_cast<float>((rand() % 200) - 100) / 10.0f;

  softmax_cpu(input, expected);

  cl::Buffer buf_input(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                       sizeof(float) * input.size(), input.data());
  cl::Buffer buf_output(context, CL_MEM_WRITE_ONLY,
                        sizeof(float) * output.size());

  cl::Kernel kernel(program, "softmax_rows");
  kernel.setArg(0, buf_input);
  kernel.setArg(1, buf_output);
  kernel.setArg(2, COLS);
  queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(ROWS),
                             cl::NullRange);
  queue.enqueueReadBuffer(buf_output, CL_TRUE, 0, sizeof(float) * output.size(),
                          output.data());

  for (size_t i = 0; i < output.size(); ++i) {
    if (std::fabs(output[i] - expected[i]) > FP_ERROR) {
      std::cerr << RED << "softmax mismatch at " << i << ": got " << output[i]
                << ", expected " << expected[i] << RESET << std::endl;
      return -1;
    }
  }

  std::cout << GREEN << "softmax passed" << RESET << std::endl;
  return 0;
}
