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
#define COLS 16
#define EPS 1e-5f
#define FP_ERROR 0.001f

#define RED "\033[0;31m"
#define GREEN "\033[0;32m"
#define RESET "\033[0m"

static void layernorm_cpu(const std::vector<float> &input,
                          const std::vector<float> &gamma,
                          const std::vector<float> &beta,
                          std::vector<float> &output) {
  for (int row = 0; row < ROWS; ++row) {
    float mean = 0.0f;
    for (int col = 0; col < COLS; ++col) {
      mean += input[row * COLS + col];
    }
    mean /= static_cast<float>(COLS);

    float var = 0.0f;
    for (int col = 0; col < COLS; ++col) {
      float diff = input[row * COLS + col] - mean;
      var += diff * diff;
    }
    var /= static_cast<float>(COLS);

    float inv_std = 1.0f / std::sqrt(var + EPS);
    for (int col = 0; col < COLS; ++col) {
      float norm = (input[row * COLS + col] - mean) * inv_std;
      output[row * COLS + col] = norm * gamma[col] + beta[col];
    }
  }
}

int main() {
  srand(3);

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
  std::vector<float> beta(COLS);
  std::vector<float> output(ROWS * COLS, 0.0f);
  std::vector<float> expected(ROWS * COLS, 0.0f);

  for (float &x : input) x = static_cast<float>((rand() % 200) - 100) / 10.0f;
  for (float &x : gamma) x = static_cast<float>((rand() % 100) + 1) / 100.0f;
  for (float &x : beta) x = static_cast<float>((rand() % 100) - 50) / 100.0f;

  layernorm_cpu(input, gamma, beta, expected);

  cl::Buffer buf_input(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                       sizeof(float) * input.size(), input.data());
  cl::Buffer buf_gamma(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                       sizeof(float) * gamma.size(), gamma.data());
  cl::Buffer buf_beta(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                      sizeof(float) * beta.size(), beta.data());
  cl::Buffer buf_output(context, CL_MEM_WRITE_ONLY,
                        sizeof(float) * output.size());

  cl::Kernel kernel(program, "layernorm");
  kernel.setArg(0, buf_input);
  kernel.setArg(1, buf_gamma);
  kernel.setArg(2, buf_beta);
  kernel.setArg(3, buf_output);
  kernel.setArg(4, COLS);
  kernel.setArg(5, EPS);

  queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(ROWS),
                             cl::NullRange);
  queue.enqueueReadBuffer(buf_output, CL_TRUE, 0, sizeof(float) * output.size(),
                          output.data());

  for (size_t i = 0; i < output.size(); ++i) {
    if (std::fabs(output[i] - expected[i]) > FP_ERROR) {
      std::cerr << RED << "layernorm mismatch at " << i << ": got " << output[i]
                << ", expected " << expected[i] << RESET << std::endl;
      return -1;
    }
  }

  std::cout << GREEN << "layernorm passed" << RESET << std::endl;
  return 0;
}
