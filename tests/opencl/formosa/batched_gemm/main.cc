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

#define BATCH 4
#define M 8
#define N 8
#define K 8
#define FP_ERROR 0.001f

#define RED "\033[0;31m"
#define GREEN "\033[0;32m"
#define RESET "\033[0m"

static void batched_gemm_cpu(const std::vector<float> &A,
                             const std::vector<float> &B,
                             std::vector<float> &C) {
  for (int batch = 0; batch < BATCH; ++batch) {
    for (int row = 0; row < M; ++row) {
      for (int col = 0; col < N; ++col) {
        float sum = 0.0f;
        for (int inner = 0; inner < K; ++inner) {
          sum += A[batch * M * K + row * K + inner] *
                 B[batch * K * N + inner * N + col];
        }
        C[batch * M * N + row * N + col] = sum;
      }
    }
  }
}

int main() {
  srand(6);

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

  std::vector<float> A(BATCH * M * K);
  std::vector<float> B(BATCH * K * N);
  std::vector<float> C(BATCH * M * N, 0.0f);
  std::vector<float> expected(BATCH * M * N, 0.0f);

  for (float &x : A) x = static_cast<float>((rand() % 200) - 100) / 15.0f;
  for (float &x : B) x = static_cast<float>((rand() % 200) - 100) / 15.0f;

  batched_gemm_cpu(A, B, expected);

  cl::Buffer buf_A(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                   sizeof(float) * A.size(), A.data());
  cl::Buffer buf_B(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                   sizeof(float) * B.size(), B.data());
  cl::Buffer buf_C(context, CL_MEM_WRITE_ONLY, sizeof(float) * C.size());

  cl::Kernel kernel(program, "batched_gemm");
  kernel.setArg(0, buf_A);
  kernel.setArg(1, buf_B);
  kernel.setArg(2, buf_C);
  kernel.setArg(3, M);
  kernel.setArg(4, N);
  kernel.setArg(5, K);

  queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(BATCH, M, N),
                             cl::NullRange);
  queue.enqueueReadBuffer(buf_C, CL_TRUE, 0, sizeof(float) * C.size(),
                          C.data());

  for (size_t i = 0; i < C.size(); ++i) {
    if (std::fabs(C[i] - expected[i]) > FP_ERROR) {
      std::cerr << RED << "batched_gemm mismatch at " << i << ": got " << C[i]
                << ", expected " << expected[i] << RESET << std::endl;
      return -1;
    }
  }

  std::cout << GREEN << "batched_gemm passed" << RESET << std::endl;
  return 0;
}
