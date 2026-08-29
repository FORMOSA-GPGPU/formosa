// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <CL/opencl.hpp>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#ifndef KERNEL_PATH
#define KERNEL_PATH "./kernel.cl"
#endif

#define ELEMENTS 1024 * 8

#define RED "\033[0;31m"
#define GREEN "\033[0;32m"
#define RESET "\033[0m"

template <typename T>
void divergence_kernel(cl::Context &context, cl::Program &program,
                       std::string_view kernel, cl::CommandQueue &queue,
                       std::vector<T> &arr, std::vector<T> &d, int arr_size) {
  cl::Buffer buf_arr(context, CL_MEM_READ_WRITE, sizeof(T) * arr_size);
  cl::Buffer buf_d(context, CL_MEM_READ_WRITE, sizeof(T) * arr_size);

  cl::Kernel kernel_func(program, kernel.data());

  kernel_func.setArg(0, buf_arr);
  kernel_func.setArg(1, buf_d);
  kernel_func.setArg(2, arr_size);

  queue.enqueueWriteBuffer(buf_arr, CL_FALSE, 0, sizeof(T) * arr_size,
                           arr.data());
  queue.enqueueWriteBuffer(buf_d, CL_FALSE, 0, sizeof(T) * arr_size, d.data());
  queue.enqueueNDRangeKernel(kernel_func, cl::NullRange, cl::NDRange(arr_size),
                             cl::NullRange);
  queue.finish();

  // A non-blocking read does not make the host pointer valid immediately.
  // Keep exercising the asynchronous D2H path, but wait for this read before
  // checking the result below.
  cl::Event read_event;
  queue.enqueueReadBuffer(buf_arr, CL_FALSE, 0, sizeof(T) * arr_size,
                          arr.data(), nullptr, &read_event);
  queue.flush();
  read_event.wait();
}

int main(int argc, char **argv) {
  srand(time(nullptr));
  std::vector<cl::Platform> platforms;
  cl::Platform::get(&platforms);

  if (platforms.empty()) {
    std::cerr << "No platforms!" << std::endl;
    return -1;
  }

  cl::Platform platform = platforms[0];
  std::vector<cl::Device> devices;

  platform.getDevices(CL_DEVICE_TYPE_GPU, &devices);
  if (devices.empty()) {
    std::cerr << "No Devices!" << std::endl;
    return -1;
  }

  cl::Device device = devices[0];
  std::cout << "Device : " << device.getInfo<CL_DEVICE_NAME>() << std::endl;
  cl::Context context({device});

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

  if (program.build() != CL_SUCCESS) {
    std::cerr << "Fail to build" << std::endl;
    return -1;
  }

  // Create input arrays
  std::vector<int> arr(ELEMENTS);
  std::vector<int> d(ELEMENTS);

  // Initialize arrays
  for (int i = 0; i < ELEMENTS; i++) {
    arr[i] = (rand() % 201) - 100;  // Random values for arr
    d[i] = (rand() % 10) - 10;      // Random values for d
  }
  std::vector<int> arr_copy = arr;
  cl::CommandQueue queue(context, device);

  // Call kernel to process divergence logic
  divergence_kernel<int>(context, program, "divergence_kernel", queue, arr, d,
                         ELEMENTS);

  // Check the result
  bool has_error = false;
  for (int i = 0; i < ELEMENTS; i++) {
    if (arr_copy[i] > 0 && arr[i] != arr_copy[i] + d[i]) {
      std::cerr << RED << "Error at " << i << ": " << arr_copy[i] << " + "
                << d[i] << " = " << arr[i] << RESET << std::endl;
      has_error = true;
    } else if (arr_copy[i] <= 0 && arr[i] != 0) {
      std::cerr << RED << "Error at " << i << ": arr[" << i << "] = " << arr[i]
                << " (Expected 0)" << RESET << std::endl;
      has_error = true;
    }
  }

  if (has_error) {
    std::cerr << RED << "Divergence Program Failed!" << RESET << std::endl;
    return -1;
  } else {
    std::cout << GREEN << "Divergence Program Passed!" << RESET << std::endl;
  }

  return 0;
}
