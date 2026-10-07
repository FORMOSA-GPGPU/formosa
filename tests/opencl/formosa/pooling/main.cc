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

#define INPUT_W 8
#define POOL 2
#define STRIDE 2
#define OUTPUT_W ((INPUT_W - POOL) / STRIDE + 1)
#define FP_ERROR 0.0001f

#define RED "\033[0;31m"
#define GREEN "\033[0;32m"
#define RESET "\033[0m"

static void max_pool_cpu(const std::vector<float> &input,
                         std::vector<float> &output) {
  for (int row = 0; row < OUTPUT_W; ++row) {
    for (int col = 0; col < OUTPUT_W; ++col) {
      float max_val = input[(row * STRIDE) * INPUT_W + col * STRIDE];
      for (int i = 0; i < POOL; ++i) {
        for (int j = 0; j < POOL; ++j) {
          float value = input[(row * STRIDE + i) * INPUT_W + col * STRIDE + j];
          max_val = std::max(max_val, value);
        }
      }
      output[row * OUTPUT_W + col] = max_val;
    }
  }
}

static void avg_pool_cpu(const std::vector<float> &input,
                         std::vector<float> &output) {
  for (int row = 0; row < OUTPUT_W; ++row) {
    for (int col = 0; col < OUTPUT_W; ++col) {
      float sum = 0.0f;
      for (int i = 0; i < POOL; ++i) {
        for (int j = 0; j < POOL; ++j) {
          sum += input[(row * STRIDE + i) * INPUT_W + col * STRIDE + j];
        }
      }
      output[row * OUTPUT_W + col] = sum / static_cast<float>(POOL * POOL);
    }
  }
}

static bool verify(const char *name, const std::vector<float> &output,
                   const std::vector<float> &expected) {
  for (size_t i = 0; i < output.size(); ++i) {
    if (std::fabs(output[i] - expected[i]) > FP_ERROR) {
      std::cerr << RED << name << " mismatch at " << i << ": got " << output[i]
                << ", expected " << expected[i] << RESET << std::endl;
      return false;
    }
  }
  std::cout << GREEN << name << " passed" << RESET << std::endl;
  return true;
}

int main() {
  srand(2);

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

  std::vector<float> input(INPUT_W * INPUT_W);
  for (float &x : input) x = static_cast<float>((rand() % 200) - 100) / 10.0f;

  std::vector<float> max_expected(OUTPUT_W * OUTPUT_W, 0.0f);
  std::vector<float> avg_expected(OUTPUT_W * OUTPUT_W, 0.0f);
  std::vector<float> max_output(OUTPUT_W * OUTPUT_W, 0.0f);
  std::vector<float> avg_output(OUTPUT_W * OUTPUT_W, 0.0f);

  max_pool_cpu(input, max_expected);
  avg_pool_cpu(input, avg_expected);

  cl::Buffer buf_input(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                       sizeof(float) * input.size(), input.data());
  cl::Buffer buf_output(context, CL_MEM_WRITE_ONLY,
                        sizeof(float) * max_output.size());

  cl::Kernel max_kernel(program, "max_pool2d");
  max_kernel.setArg(0, buf_input);
  max_kernel.setArg(1, buf_output);
  max_kernel.setArg(2, INPUT_W);
  max_kernel.setArg(3, POOL);
  max_kernel.setArg(4, STRIDE);
  max_kernel.setArg(5, OUTPUT_W);
  queue.enqueueNDRangeKernel(max_kernel, cl::NullRange,
                             cl::NDRange(max_output.size()), cl::NullRange);
  queue.enqueueReadBuffer(buf_output, CL_TRUE, 0,
                          sizeof(float) * max_output.size(), max_output.data());

  cl::Kernel avg_kernel(program, "avg_pool2d");
  avg_kernel.setArg(0, buf_input);
  avg_kernel.setArg(1, buf_output);
  avg_kernel.setArg(2, INPUT_W);
  avg_kernel.setArg(3, POOL);
  avg_kernel.setArg(4, STRIDE);
  avg_kernel.setArg(5, OUTPUT_W);
  queue.enqueueNDRangeKernel(avg_kernel, cl::NullRange,
                             cl::NDRange(avg_output.size()), cl::NullRange);
  queue.enqueueReadBuffer(buf_output, CL_TRUE, 0,
                          sizeof(float) * avg_output.size(), avg_output.data());

  bool passed = true;
  passed &= verify("max_pool2d", max_output, max_expected);
  passed &= verify("avg_pool2d", avg_output, avg_expected);
  return passed ? 0 : -1;
}
