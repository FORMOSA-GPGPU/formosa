// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <CL/opencl.hpp>
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

constexpr int kLocalSize = 64;
constexpr int kNumGroups = 8;
constexpr int kIters = 256;

}  // namespace

int main() {
  const size_t n = static_cast<size_t>(kLocalSize) * kNumGroups;
  std::vector<int32_t> output(n, 0);

  std::cout << "Benchmark: lmem_bank_parallel\n"
            << "local_size: " << kLocalSize << "\n"
            << "num_groups: " << kNumGroups << "\n"
            << "iters: " << kIters << "\n";

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

    cl::Context context({devices[0]});
    cl::CommandQueue queue(context, devices[0]);

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
                << program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(devices[0])
                << "\n";
      return 1;
    }

    cl::Buffer output_buffer(context, CL_MEM_WRITE_ONLY,
                             sizeof(int32_t) * output.size());

    cl::Kernel kernel(program, "lmem_bank_parallel");
    kernel.setArg(0, output_buffer);
    kernel.setArg(1, cl::Local(sizeof(int32_t) * kLocalSize));
    kernel.setArg(2, kIters);

    queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(n),
                               cl::NDRange(kLocalSize));
    queue.enqueueReadBuffer(output_buffer, CL_TRUE, 0,
                            sizeof(int32_t) * output.size(), output.data());

    for (size_t i = 0; i < n; ++i) {
      if (output[i] != kIters) {
        std::cout << "Validation: FAIL\n"
                  << "Mismatch at index " << i << "\n"
                  << "expected: " << kIters << "\n"
                  << "got: " << output[i] << "\n";
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
