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

#define VOCAB 32
#define TOKENS 12
#define DIM 16
#define FP_ERROR 0.0001f

#define RED "\033[0;31m"
#define GREEN "\033[0;32m"
#define RESET "\033[0m"

int main() {
  srand(5);

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

  std::vector<float> table(VOCAB * DIM);
  std::vector<int> indices(TOKENS);
  std::vector<float> output(TOKENS * DIM, 0.0f);
  std::vector<float> expected(TOKENS * DIM, 0.0f);

  for (float &x : table) x = static_cast<float>((rand() % 200) - 100) / 20.0f;
  for (int &idx : indices) idx = rand() % VOCAB;

  for (int token = 0; token < TOKENS; ++token) {
    for (int col = 0; col < DIM; ++col) {
      expected[token * DIM + col] = table[indices[token] * DIM + col];
    }
  }

  cl::Buffer buf_table(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                       sizeof(float) * table.size(), table.data());
  cl::Buffer buf_indices(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                         sizeof(int) * indices.size(), indices.data());
  cl::Buffer buf_output(context, CL_MEM_WRITE_ONLY,
                        sizeof(float) * output.size());

  cl::Kernel kernel(program, "embedding_lookup");
  kernel.setArg(0, buf_table);
  kernel.setArg(1, buf_indices);
  kernel.setArg(2, buf_output);
  kernel.setArg(3, DIM);
  queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(output.size()),
                             cl::NullRange);
  queue.enqueueReadBuffer(buf_output, CL_TRUE, 0, sizeof(float) * output.size(),
                          output.data());

  for (size_t i = 0; i < output.size(); ++i) {
    if (std::fabs(output[i] - expected[i]) > FP_ERROR) {
      std::cerr << RED << "embedding mismatch at " << i << ": got " << output[i]
                << ", expected " << expected[i] << RESET << std::endl;
      return -1;
    }
  }

  std::cout << GREEN << "embedding passed" << RESET << std::endl;
  return 0;
}
