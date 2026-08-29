// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <unistd.h>

#include <CL/opencl.hpp>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <type_traits>
#include <vector>

#ifndef KERNEL_PATH
#define KERNEL_PATH "./kernel.cl"
#endif

#define ELEMENTS (16 * 4 * 2)

#define RED "\033[0;31m"
#define GREEN "\033[0;32m"
#define RESET "\033[0m"

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

  // Create the program
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

  // Create the buffer
  std::vector<int> A(ELEMENTS);
  vector_random<int>(A);  // Randomize the vector
  cl::Buffer buf_A(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                   sizeof(int) * ELEMENTS, A.data());

  // Build the program
  if (program.build() != CL_SUCCESS) {
    std::cerr << "Fail to build" << std::endl;
    return -1;
  }

  // Set the kernel
  cl::Kernel oclprintf(program, "oclprintf");
  oclprintf.setArg(0, buf_A);

  FILE *fp = fopen("printf.log", "w+");
  if (!fp) {
    std::cerr << RED << "Failed to open printf.log" << RESET << std::endl;
    return -1;
  }

  std::cout.flush();
  fflush(stdout);
  int saved_stdout = dup(fileno(stdout));
  if (saved_stdout < 0) {
    std::perror("dup");
    fclose(fp);
    return -1;
  }

  if (dup2(fileno(fp), fileno(stdout)) < 0) {
    std::perror("dup2");
    close(saved_stdout);
    fclose(fp);
    return -1;
  }

  // Execute the kernel
  cl::NDRange globalSize(ELEMENTS);
  queue.enqueueNDRangeKernel(oclprintf, cl::NullRange, globalSize,
                             cl::NullRange);
  queue.finish();
  fflush(stdout);

  if (dup2(saved_stdout, fileno(stdout)) < 0) {
    std::perror("dup2");
    close(saved_stdout);
    fclose(fp);
    return -1;
  }
  close(saved_stdout);

  // Check the result
  // Read the result from printf.log captured from stdout during kernel run.
  rewind(fp);

  // Parse the printf.log line by line
  char line[256];
  int num_lines = 0;
  std::vector<int> A_check(ELEMENTS);
  while (fgets(line, sizeof(line), fp)) {
    int idx, val;
    // read the line
    if (sscanf(line, "A[%d] = %d", &idx, &val) != 2) {
      std::cerr << RED << "Failed to parse printf.log" << RESET << std::endl;
      fclose(fp);
      return -1;
    }
    // Check and store the value
    if (idx < 0 || idx >= ELEMENTS) {
      std::cerr << RED << "Index out of range" << RESET << std::endl;
      fclose(fp);
      return -1;
    }
    A_check[idx] = val;
    num_lines++;
  }

  if (num_lines != ELEMENTS) {
    std::cerr << RED << "Number of lines in printf.log is not equal to "
              << ELEMENTS << RESET << std::endl;
    fclose(fp);
    return -1;
  }

  bool has_error = false;
  for (int i = 0; i < ELEMENTS; i++) {
    if (!compare(A[i], A_check[i], 0)) {
      std::cerr << RED << "Error at " << i << " : expected " << A[i]
                << " but got " << A_check[i] << RESET << std::endl;
      has_error = true;
    }
  }

  if (has_error) {
    std::cerr << RED << "Oclprintf Program Failed!" << RESET << std::endl;
  } else {
    std::cout << GREEN << "Oclprintf Program Passed!" << RESET << std::endl;
  }

  fclose(fp);
  return has_error;
}
