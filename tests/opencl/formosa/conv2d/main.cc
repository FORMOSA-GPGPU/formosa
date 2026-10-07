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

#define INPUT_SIZE 32
#define KERNEL_SIZE 3
#define STRIDE 1
#define FP_ERROR 0.01
#define DP_ERROR 0.0000001

template <typename T>
void conv2d_cpu(const std::vector<T> &input, const std::vector<T> &kernel,
                std::vector<T> &output, const int output_size) {
  for (int x = 0; x < output_size; x++) {
    for (int y = 0; y < output_size; y++) {
      T sum = 0;
      for (int i = 0; i < KERNEL_SIZE; i++) {
        for (int j = 0; j < KERNEL_SIZE; j++) {
          int input_x = x * STRIDE + i;
          int input_y = y * STRIDE + j;
          if (input_x < INPUT_SIZE && input_y < INPUT_SIZE) {
            sum += input[input_x * INPUT_SIZE + input_y] *
                   kernel[i * KERNEL_SIZE + j];
          }
        }
      }
      output[x * output_size + y] = sum;
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
  const int i = INPUT_SIZE;
  const int k = KERNEL_SIZE;
  const int s = STRIDE;
  const int OUTPUT_SIZE = (INPUT_SIZE - KERNEL_SIZE) / STRIDE + 1;

  std::vector<T> input(INPUT_SIZE * INPUT_SIZE);
  std::vector<T> kernel(KERNEL_SIZE * KERNEL_SIZE);
  std::vector<T> output(OUTPUT_SIZE * OUTPUT_SIZE);
  std::vector<T> output_cpu(OUTPUT_SIZE * OUTPUT_SIZE);

  vector_random<T>(input);
  vector_random<T>(kernel);

  // Create buffers
  cl::Buffer buf_input(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                       sizeof(T) * INPUT_SIZE * INPUT_SIZE, input.data());
  cl::Buffer buf_kernel(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                        sizeof(T) * KERNEL_SIZE * KERNEL_SIZE, kernel.data());
  cl::Buffer buf_output(context, CL_MEM_READ_WRITE,
                        sizeof(T) * OUTPUT_SIZE * OUTPUT_SIZE);

  // Build program
  std::string build_option = "-D TYPE=" + type_name;
  if (program.build(build_option.c_str()) != CL_SUCCESS) {
    std::cerr << "Fail to build" << std::endl;
    return false;
  }

  // Set the kernel
  cl::Kernel conv2d(program, "conv2d");

  conv2d.setArg(0, buf_input);
  conv2d.setArg(1, buf_kernel);
  conv2d.setArg(2, buf_output);
  conv2d.setArg(3, i);
  conv2d.setArg(4, k);
  conv2d.setArg(5, s);

  // Execute the kernel
  cl::NDRange globalSize(OUTPUT_SIZE, OUTPUT_SIZE);
  queue.enqueueNDRangeKernel(conv2d, cl::NullRange, globalSize, cl::NullRange);
  // Read the result
  queue.enqueueReadBuffer(buf_output, CL_FALSE, 0,
                          sizeof(T) * OUTPUT_SIZE * OUTPUT_SIZE, output.data());
  queue.finish();

  // CPU execute the kernel
  conv2d_cpu<T>(input, kernel, output_cpu, OUTPUT_SIZE);

  // Check the result
  bool has_error = false;
  for (int i = 0; i < OUTPUT_SIZE * OUTPUT_SIZE; i++) {
    if (!compare<T>(output[i], output_cpu[i], error)) {
      std::cerr << "Error at " << "output[" << i << "] : " << output[i]
                << "\tExpected = " << output_cpu[i] << std::endl;
      has_error = true;
    }
  }

  if (has_error) {
    std::cerr << "Conv2d " << type_name << " Failed!" << std::endl;
    return false;
  } else {
    std::cout << "Conv2d " << type_name << " Passed!" << std::endl;
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
