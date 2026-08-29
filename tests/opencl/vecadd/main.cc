#include <CL/opencl.hpp>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef KERNEL_PATH
#define KERNEL_PATH "./kernel.cl"
#endif

#define ELEMENTS 1024
#define FP_ERROR 0.00001

#define RED "\033[0;31m"
#define GREEN "\033[0;32m"
#define RESET "\033[0m"

using SetKernelStackRemapFn = cl_int(CL_API_CALL *)(cl_kernel, cl_bool);

bool fp_error(float a, float b) {
  return (a - b) > FP_ERROR || (b - a) > FP_ERROR;
}

template <typename T>
void vecadd(cl::Context &context, cl::Program &program, std::string_view kernel,
            cl::CommandQueue &queue, std::vector<T> &A, std::vector<T> &B,
            std::vector<T> &C, SetKernelStackRemapFn set_stack_remap,
            cl_bool designate) {
  cl::Buffer buf_A(context, CL_MEM_READ_WRITE, sizeof(T) * ELEMENTS);
  cl::Buffer buf_B(context, CL_MEM_READ_WRITE, sizeof(T) * ELEMENTS);
  cl::Buffer buf_C(context, CL_MEM_READ_WRITE, sizeof(T) * ELEMENTS);

  cl::Kernel vec_add(program, kernel.data());
  if (set_stack_remap != nullptr) {
    cl_int err = set_stack_remap(vec_add(), designate);
    if (err != CL_SUCCESS) {
      throw std::runtime_error("clSetKernelStackRemapFORMOSA failed: " +
                               std::to_string(err));
    }
  }

  vec_add.setArg(0, buf_A);
  vec_add.setArg(1, buf_B);
  vec_add.setArg(2, buf_C);

  queue.enqueueWriteBuffer(buf_A, CL_FALSE, 0, sizeof(T) * ELEMENTS, A.data());
  queue.enqueueWriteBuffer(buf_B, CL_FALSE, 0, sizeof(T) * ELEMENTS, B.data());
  queue.enqueueNDRangeKernel(vec_add, cl::NullRange, cl::NDRange(ELEMENTS),
                             cl::NullRange);
  queue.enqueueReadBuffer(buf_C, CL_FALSE, 0, sizeof(T) * ELEMENTS, C.data());
  queue.finish();
}

int main(int argc, char **argv) {
  bool explicit_stack_remap = false;
  cl_bool stack_remap_value = CL_TRUE;
  if (argc == 2 && std::string_view(argv[1]) == "--stack-remap") {
    explicit_stack_remap = true;
  } else if (argc == 2 && std::string_view(argv[1]) == "--no-stack-remap") {
    explicit_stack_remap = true;
    stack_remap_value = CL_FALSE;
  } else if (argc != 1) {
    std::cerr << "Usage: " << argv[0] << " [--stack-remap|--no-stack-remap]\n";
    return -1;
  }

  srand(time(nullptr));
  std::vector<cl::Platform> platforms;
  cl::Platform::get(&platforms);

  if (platforms.empty()) {
    std::cerr << "No platforms!" << std::endl;
    return -1;
  }

  cl::Platform platform = platforms[0];
  std::vector<cl::Device> Devices;

  platform.getDevices(CL_DEVICE_TYPE_GPU, &Devices);
  if (Devices.empty()) {
    std::cerr << "No Devices!" << std::endl;
    return -1;
  }

  cl::Device device = Devices[0];
  std::cout << "Device : " << device.getInfo<CL_DEVICE_NAME>() << std::endl;

  SetKernelStackRemapFn set_stack_remap = nullptr;
  if (explicit_stack_remap) {
    set_stack_remap = reinterpret_cast<SetKernelStackRemapFn>(
        clGetExtensionFunctionAddressForPlatform(
            platform(), "clSetKernelStackRemapFORMOSA"));
    if (set_stack_remap == nullptr) {
      std::cerr << "clSetKernelStackRemapFORMOSA is unavailable\n";
      return -1;
    }
  }
  std::cout << "Stack remap designation: "
            << (!explicit_stack_remap
                    ? "enabled (default)"
                    : (stack_remap_value == CL_TRUE ? "enabled" : "disabled"))
            << std::endl;

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

  // Integer test
  std::vector<int> A(ELEMENTS);
  std::vector<int> B(ELEMENTS);
  std::vector<int> C(ELEMENTS);

  for (int i = 0; i < ELEMENTS; i++) {
    A[i] = rand() % 100;
    B[i] = rand() % 100;
  }

  cl::CommandQueue queue(context, device);

  vecadd<int>(context, program, "vecadd", queue, A, B, C, set_stack_remap,
              stack_remap_value);

  // Check the result
  bool has_error = false;
  for (int i = 0; i < ELEMENTS; i++) {
    if (A[i] + B[i] != C[i]) {
      std::cerr << RED << "Error at " << i << " : " << A[i] << " + " << B[i]
                << " = " << C[i] << "\tExpected = " << A[i] + B[i] << RESET
                << std::endl;
      has_error = true;
    }
  }

  if (has_error) {
    std::cerr << RED << "Vecadd Program Failed!" << RESET << std::endl;
    return -1;
  } else {
    std::cout << GREEN << "Vecadd Program Passed! (int)" << RESET << std::endl;
  }

  // Floating point test
  std::vector<float> A_float(ELEMENTS);
  std::vector<float> B_float(ELEMENTS);
  std::vector<float> C_float(ELEMENTS);

  for (int i = 0; i < ELEMENTS; i++) {
    A_float[i] = rand() % 100 / 10.0;
    B_float[i] = rand() % 100 / 10.0;
  }

  vecadd<float>(context, program, "vecadd_float", queue, A_float, B_float,
                C_float, set_stack_remap, stack_remap_value);

  // Check the result
  for (int i = 0; i < ELEMENTS; i++) {
    if (fp_error(A_float[i] + B_float[i], C_float[i])) {
      std::cerr << RED << "Error at " << i << " : " << A_float[i] << " + "
                << B_float[i] << " = " << C_float[i]
                << "\tExpected = " << A_float[i] + B_float[i] << RESET
                << std::endl;
      has_error = true;
    }
  }

  if (has_error) {
    std::cerr << RED << "Vecadd Program Failed!" << RESET << std::endl;
    return -1;
  } else {
    std::cout << GREEN << "Vecadd Program Passed! (float)" << RESET
              << std::endl;
  }

  // Double floating point test
  std::vector<double> A_double(ELEMENTS);
  std::vector<double> B_double(ELEMENTS);
  std::vector<double> C_double(ELEMENTS);

  for (int i = 0; i < ELEMENTS; i++) {
    A_double[i] = rand() % 100 / 10.0;
    B_double[i] = rand() % 100 / 10.0;
  }

  vecadd<double>(context, program, "vecadd_double", queue, A_double, B_double,
                 C_double, set_stack_remap, stack_remap_value);

  // Check the result
  for (int i = 0; i < ELEMENTS; i++) {
    if (fp_error(A_double[i] + B_double[i], C_double[i])) {
      std::cerr << RED << "Error at " << i << " : " << A_double[i] << " + "
                << B_double[i] << " = " << C_double[i]
                << "\tExpected = " << A_double[i] + B_double[i] << RESET
                << std::endl;
      has_error = true;
    }
  }

  if (has_error) {
    std::cerr << RED << "Vecadd Program Failed!" << RESET << std::endl;
    return -1;
  } else {
    std::cout << GREEN << "Vecadd Program Passed! (double)" << RESET
              << std::endl;
  }

  return 0;
}
