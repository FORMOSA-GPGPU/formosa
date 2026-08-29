// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <sys/types.h>

#include <CL/opencl.hpp>
#include <fstream>
#include <iostream>

#ifndef KERNEL_PATH
#define KERNEL_PATH "./kernel.cl"
#endif

#define NUM_WG 4
#define LOCAL_SIZE 4
#define LMEMSIZE 1024

#define RED "\033[0;31m"
#define GREEN "\033[0;32m"
#define RESET "\033[0m"

int main() {
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
  std::string build_options = "-D LMEMSIZE=" + std::to_string(LMEMSIZE);
  if (program.build(build_options.c_str()) != CL_SUCCESS) {
    std::cerr << "Fail to build" << std::endl;
    return -1;
  }

  std::vector<cl_uint> output(NUM_WG, 0);

  cl::Buffer buf_output(context, CL_MEM_WRITE_ONLY,
                        sizeof(output[0]) * output.size());

  cl::Kernel kernel(program, "lmem_allocate");
  kernel.setArg(0, buf_output);

  queue.enqueueNDRangeKernel(kernel, cl::NullRange,
                             cl::NDRange(NUM_WG * LOCAL_SIZE),
                             cl::NDRange(LOCAL_SIZE));
  queue.enqueueReadBuffer(buf_output, CL_TRUE, 0,
                          sizeof(output[0]) * output.size(), output.data());

  bool pass = true;

  for (size_t i = 0; i < output.size(); ++i) {
    cl_uint expected =
        LOCAL_SIZE * LOCAL_SIZE * i + LOCAL_SIZE * (LOCAL_SIZE - 1) / 2;
    if (output[i] != expected) {
      std::cerr << "Test failed at workgroup " << i << ": expected " << expected
                << " but got " << output[i] << std::endl;
      pass = false;
    }
  }

  if (pass) {
    std::cout << GREEN << "lmem_allocate passed" << RESET << std::endl;
    return 0;
  } else {
    std::cout << RED << "lmem_allocate failed" << RESET << std::endl;
    return 1;
  }
}
