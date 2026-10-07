// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <CL/opencl.hpp>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#ifndef KERNEL_PATH
#define KERNEL_PATH "./kernel.cl"
#endif

#define M 64
#define N 64
#define K 64
#define FP_ERROR 0.01
#define DP_ERROR 0.0000001

#define RED "\033[0;31m"
#define GREEN "\033[0;32m"
#define RESET "\033[0m"

template <typename T>
void gemm_cpu(const std::vector<T> &A, const std::vector<T> &B,
              std::vector<T> &C) {
  for (int i = 0; i < M; i++) {    // row
    for (int j = 0; j < N; j++) {  // column
      T sum = 0;
      for (int k = 0; k < K; k++) {
        sum += A[i * K + k] * B[k * N + j];
      }
      C[i * N + j] = sum;
    }
  }
}

template <typename T>
void vector_random(std::vector<T> &vec) {
  if constexpr (std::is_integral_v<T>) {
    for (int i = 0; i < vec.size(); i++) {
      vec[i] = rand() % 100;
    }
  } else if constexpr (std::is_floating_point_v<T>) {
    for (int i = 0; i < vec.size(); i++) {
      vec[i] = rand() % 100 / 10.0;
    }
  }
}

// Compare two values
// Return true if they are equal
template <typename T>
bool compare(T a, T b, T error) {
  if constexpr (std::is_integral_v<T>) {
    return a == b;
  } else if constexpr (std::is_floating_point_v<T>) {
    return std::fabs(a - b) < error;
  }
}

// build_and_test
// Return true if the test passed
template <typename T>
bool build_and_test(cl::CommandQueue &queue, cl::Context &context,
                    cl::Program &program, std::string type_name, T error = 0) {
  std::vector<T> A(M * K);
  std::vector<T> B(K * N);
  std::vector<T> C(M * N);
  std::vector<T> C_cpu(M * N);

  vector_random<T>(A);
  vector_random<T>(B);

  // Create buffers
  cl::Buffer buf_A(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                   sizeof(T) * M * K, A.data());
  cl::Buffer buf_B(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                   sizeof(T) * K * N, B.data());
  cl::Buffer buf_C(context, CL_MEM_READ_WRITE, sizeof(T) * M * N);

  // Build program
  std::string build_option = "-D TYPE=" + type_name;
  if (program.build(build_option.c_str()) != CL_SUCCESS) {
    std::cerr << "Fail to build" << std::endl;
    return false;
  }

  // Set the kernel
  cl::Kernel gemm(program, "gemm");

  int k = K;

  gemm.setArg(0, buf_A);
  gemm.setArg(1, buf_B);
  gemm.setArg(2, buf_C);
  gemm.setArg(3, k);

  // Execute the kernel
  cl::NDRange globalSize(M, N);
  queue.enqueueNDRangeKernel(gemm, cl::NullRange, globalSize, cl::NullRange);
  // Read the result
  queue.enqueueReadBuffer(buf_C, CL_FALSE, 0, sizeof(T) * M * N, C.data());
  queue.finish();

  // CPU execute the kernel
  gemm_cpu<T>(A, B, C_cpu);

  // Check the result
  bool has_error = false;
  for (int i = 0; i < M * N; i++) {
    if (!compare<T>(C[i], C_cpu[i], error)) {
      std::cerr << RED << "Error at " << "C[" << i << "] : " << C[i]
                << "\tExpected = " << C_cpu[i] << RESET << std::endl;
      has_error = true;
    }
  }

  if (has_error) {
    std::cerr << RED << "Gemm " << type_name << " Failed!" << RESET
              << std::endl;
    return false;
  } else {
    std::cout << GREEN << "Gemm " << type_name << " Passed!" << RESET
              << std::endl;
    return true;
  }
}

int main(int argc, char **argv) {
  srand(0);

  // Select the platform
  std::vector<cl::Platform> platforms;
  cl::Platform::get(&platforms);

  if (platforms.empty()) {
    std::cerr << "No platforms!" << std::endl;
    return -1;
  }

  cl::Platform platform = platforms[0];

  // Select the device
  std::vector<cl::Device> Devices;

  platform.getDevices(CL_DEVICE_TYPE_GPU, &Devices);
  if (Devices.empty()) {
    std::cerr << "No Devices!" << std::endl;
    return -1;
  }

  cl::Device device = Devices[0];

  // Create the context
  cl::Context context({device});

  // Create the command queue
  cl::CommandQueue queue(context, device);

  // Create and build the program
  std::string kernel_path = std::string(KERNEL_PATH);
  std::cout << "Kernel Path : " << kernel_path << std::endl;
  std::ifstream t(kernel_path);
  if (!t) {
    std::cerr << "Error Opening Kernel Source file\n";
    return -1;
  }
  std::string opencl_kernel = {std::istreambuf_iterator<char>(t),
                               std::istreambuf_iterator<char>()};
  cl::Program program(context, opencl_kernel);

  bool passed = true;

  // Integer test
  passed &= build_and_test<int>(queue, context, program, "int", 0);

  // Float test
  passed &= build_and_test<float>(queue, context, program, "float", FP_ERROR);

  // Double test
  passed &= build_and_test<double>(queue, context, program, "double", DP_ERROR);

  return passed ? 0 : -1;
}
