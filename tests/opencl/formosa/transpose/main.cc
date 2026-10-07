#include <CL/opencl.hpp>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

#ifndef KERNEL_PATH
#define KERNEL_PATH "./kernel.cl"
#endif

#define M 64
#define N 64
#define FP_ERROR 0.01
#define DP_ERROR 0.0000001

#define RED "\033[0;31m"
#define GREEN "\033[0;32m"
#define RESET "\033[0m"

template <typename T>
void transpose_cpu(const std::vector<T> &A, std::vector<T> &B) {
  for (int i = 0; i < M; i++) {
    for (int j = 0; j < N; j++) {
      B[j * M + i] = A[i * N + j];
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

// Build and test
// Return true if the test passed
template <typename T>
bool build_and_test(cl::CommandQueue &queue, cl::Context &context,
                    cl::Program &program, std::string type_name, T error = 0) {
  std::vector<T> A(M * N);
  std::vector<T> B(N * M);
  std::vector<T> B_cpu(N * M);

  vector_random<T>(A);

  // Create buffers
  cl::Buffer buf_A(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                   sizeof(T) * M * N, A.data());
  cl::Buffer buf_B(context, CL_MEM_READ_WRITE, sizeof(T) * N * M);

  // Build program
  std::string build_option = "-D TYPE=" + type_name;
  if (program.build(build_option.c_str()) != CL_SUCCESS) {
    std::cerr << "Fail to build" << std::endl;
    return false;
  }

  // Set the kernel
  cl::Kernel transpose(program, "transpose");

  transpose.setArg(0, buf_A);
  transpose.setArg(1, buf_B);
  transpose.setArg(2, M);
  transpose.setArg(3, N);

  // Execute the kernel
  cl::NDRange globalSize(M, N);
  queue.enqueueNDRangeKernel(transpose, cl::NullRange, globalSize,
                             cl::NullRange);

  // Read the result
  queue.enqueueReadBuffer(buf_B, CL_FALSE, 0, sizeof(T) * N * M, B.data());
  queue.finish();

  // CPU execute the kernel
  transpose_cpu<T>(A, B_cpu);

  // Check the result
  bool has_error = false;
  for (int i = 0; i < M * N; i++) {
    if (!compare<T>(B[i], B_cpu[i], error)) {
      std::cerr << RED << "Error at " << "B[" << i << "] : " << B[i]
                << "\tExpected = " << B_cpu[i] << RESET << std::endl;
      has_error = true;
    }
  }

  if (has_error) {
    std::cerr << RED << "Transpose " << type_name << " Failed!" << RESET
              << std::endl;
    return false;
  } else {
    std::cout << GREEN << "Transpose " << type_name << " Passed!" << RESET
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
