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

#define ROWS 8
#define COLS 8
#define EPS 1e-5f
#define FP_ERROR 0.001f

#define RED "\033[0;31m"
#define GREEN "\033[0;32m"
#define RESET "\033[0m"

static void rmsnorm_cpu(const std::vector<float> &input,
                        const std::vector<float> &gamma,
                        std::vector<float> &output) {
  for (int row = 0; row < ROWS; ++row) {
    float mean_sq = 0.0f;
    for (int col = 0; col < COLS; ++col) {
      float x = input[row * COLS + col];
      mean_sq += x * x;
    }
    mean_sq /= static_cast<float>(COLS);

    float inv_rms = 1.0f / std::sqrt(mean_sq + EPS);
    for (int col = 0; col < COLS; ++col) {
      output[row * COLS + col] = input[row * COLS + col] * inv_rms * gamma[col];
    }
  }
}

int main() {
  srand(12);

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
  std::vector<float> gamma(COLS);
  std::vector<float> output(ROWS * COLS, 0.0f);
  std::vector<float> expected(ROWS * COLS, 0.0f);

  for (float &x : input) x = static_cast<float>((rand() % 200) - 100) / 10.0f;
  for (float &x : gamma) x = static_cast<float>((rand() % 100) + 1) / 100.0f;

  rmsnorm_cpu(input, gamma, expected);

  cl::Buffer buf_input(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                       sizeof(float) * input.size(), input.data());
  cl::Buffer buf_gamma(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                       sizeof(float) * gamma.size(), gamma.data());
  cl::Buffer buf_output(context, CL_MEM_WRITE_ONLY,
                        sizeof(float) * output.size());

  cl::Kernel kernel(program, "rmsnorm");
  kernel.setArg(0, buf_input);
  kernel.setArg(1, buf_gamma);
  kernel.setArg(2, buf_output);
  kernel.setArg(3, COLS);
  kernel.setArg(4, EPS);

  std::cout << "Launching rmsnorm reproducer" << std::endl;
  queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(ROWS),
                             cl::NullRange);
  queue.enqueueReadBuffer(buf_output, CL_TRUE, 0, sizeof(float) * output.size(),
                          output.data());

  for (size_t i = 0; i < output.size(); ++i) {
    if (std::fabs(output[i] - expected[i]) > FP_ERROR) {
      std::cerr << RED << "rmsnorm mismatch at " << i << ": got " << output[i]
                << ", expected " << expected[i] << RESET << std::endl;
      return -1;
    }
  }

  std::cout << GREEN << "rmsnorm reproducer passed" << RESET << std::endl;
  return 0;
}
