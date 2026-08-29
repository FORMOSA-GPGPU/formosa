// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <CL/opencl.hpp>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#ifndef KERNEL_PATH
#define KERNEL_PATH "./kernel.cl"
#endif

namespace {

constexpr size_t kInputSize = 256;
constexpr size_t kPreferredLocalSize = 256;

std::vector<int32_t> inclusive_prefix_sum_cpu(
    const std::vector<int32_t> &input) {
  std::vector<int32_t> output(input.size());
  int32_t sum = 0;
  for (size_t i = 0; i < input.size(); ++i) {
    sum += input[i];
    output[i] = sum;
  }
  return output;
}

}  // namespace

int main() {
  static_assert(sizeof(int32_t) == sizeof(cl_int));

  std::cout << "Benchmark: prefix_sum\n"
            << "Mode: inclusive scan\n"
            << "Input size: " << kInputSize << "\n";

  std::vector<int32_t> input(kInputSize);
  for (size_t i = 0; i < input.size(); ++i) {
    input[i] = static_cast<int32_t>((i % 7) + 1);
  }

  const std::vector<int32_t> expected = inclusive_prefix_sum_cpu(input);
  std::cout << "CPU reference: done\n";

  try {
    std::vector<cl::Platform> platforms;
    cl::Platform::get(&platforms);
    if (platforms.empty()) {
      std::cerr << "OpenCL error: no platforms found\n";
      return 1;
    }

    std::vector<cl::Device> devices;
    platforms[0].getDevices(CL_DEVICE_TYPE_GPU, &devices);
    if (devices.empty()) {
      std::cerr << "OpenCL error: no GPU devices found\n";
      return 1;
    }

    const cl::Device &device = devices[0];
    cl::Context context({device});
    cl::CommandQueue queue(context, device);

    std::ifstream kernel_file(KERNEL_PATH);
    if (!kernel_file) {
      std::cerr << "OpenCL error: cannot open kernel source " << KERNEL_PATH
                << "\n";
      return 1;
    }
    const std::string source = {std::istreambuf_iterator<char>(kernel_file),
                                std::istreambuf_iterator<char>()};
    cl::Program program(context, source);
    try {
      program.build();
    } catch (const cl::Error &) {
      std::cerr << "OpenCL program build failed:\n"
                << program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(device) << "\n";
      return 1;
    }

    cl::Buffer input_buffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                            sizeof(int32_t) * input.size(), input.data());
    cl::Buffer output_buffer(context, CL_MEM_WRITE_ONLY,
                             sizeof(int32_t) * input.size());

    cl::Kernel kernel(program, "inclusive_prefix_sum");
    const size_t device_max_work_group_size =
        device.getInfo<CL_DEVICE_MAX_WORK_GROUP_SIZE>();
    const size_t kernel_max_work_group_size =
        kernel.getWorkGroupInfo<CL_KERNEL_WORK_GROUP_SIZE>(device);
    const size_t local_size =
        std::min({kPreferredLocalSize, device_max_work_group_size,
                  kernel_max_work_group_size});
    if (local_size == 0) {
      std::cerr << "OpenCL error: device reports a zero work-group limit\n";
      return 1;
    }
    std::cout << "Local size: " << local_size;
    if (local_size != kPreferredLocalSize) {
      std::cout << " (preferred " << kPreferredLocalSize
                << ", limited by device)";
    }
    std::cout << "\n";

    kernel.setArg(0, input_buffer);
    kernel.setArg(1, output_buffer);
    kernel.setArg(2, cl::Local(sizeof(int32_t) * kInputSize));
    kernel.setArg(3, cl::Local(sizeof(int32_t) * kInputSize));
    kernel.setArg(4, static_cast<cl_uint>(kInputSize));

    queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(local_size),
                               cl::NDRange(local_size));

    std::vector<int32_t> output(kInputSize);
    queue.enqueueReadBuffer(output_buffer, CL_TRUE, 0,
                            sizeof(int32_t) * output.size(), output.data());
    std::cout << "OpenCL kernel: done\n";

    for (size_t i = 0; i < output.size(); ++i) {
      if (output[i] != expected[i]) {
        std::cout << "Validation: FAIL\n"
                  << "Mismatch at index " << i << "\n"
                  << "CPU reference: " << expected[i] << "\n"
                  << "OpenCL output: " << output[i] << "\n";
        return 1;
      }
    }
  } catch (const cl::Error &error) {
    std::cerr << "OpenCL error in " << error.what() << ": " << error.err()
              << "\n";
    return 1;
  }

  std::cout << "Validation: PASS\n";
  return 0;
}
