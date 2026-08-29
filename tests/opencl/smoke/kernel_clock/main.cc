// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <CL/opencl.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef KERNEL_PATH
#define KERNEL_PATH "kernel.cl"
#endif

static std::string loadFile(const std::string &path) {
  std::ifstream file(path);

  if (!file.is_open()) {
    throw std::runtime_error("Failed to open file: " + path);
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

static bool hasExtension(const std::string &exts, const std::string &name) {
  return exts.find(name) != std::string::npos;
}

static void printPair(const std::string &name, cl_ulong t0, cl_ulong t1) {
  std::cout << name << "\n";
  std::cout << "  t0        = " << t0 << "\n";
  std::cout << "  t1        = " << t1 << "\n";
  std::cout << "  delta     = " << (t1 - t0) << "\n";
  std::cout << "  monotonic = " << ((t1 >= t0) ? "PASS" : "FAIL") << "\n";
}

int main() {
  try {
    std::vector<cl::Platform> platforms;
    cl::Platform::get(&platforms);

    if (platforms.empty()) {
      std::cerr << "No OpenCL platforms found.\n";
      return 1;
    }

    cl::Device device;
    bool found = false;

    for (const auto &platform : platforms) {
      std::vector<cl::Device> devices;
      platform.getDevices(CL_DEVICE_TYPE_GPU, &devices);

      for (const auto &dev : devices) {
        std::string exts = dev.getInfo<CL_DEVICE_EXTENSIONS>();

        if (hasExtension(exts, "cl_khr_kernel_clock")) {
          device = dev;
          found = true;
          break;
        }
      }

      if (found) {
        break;
      }
    }

    if (!found) {
      std::cerr << "No GPU device supports cl_khr_kernel_clock.\n";
      return 2;
    }

    std::cout << "Using device: " << device.getInfo<CL_DEVICE_NAME>() << "\n";

    cl::Context context(device);
    cl::CommandQueue queue(context, device);

    std::string source = loadFile(KERNEL_PATH);

    cl::Program program(context, source);

    try {
      program.build({device});
    } catch (const cl::Error &) {
      std::cerr << "Build failed.\n";
      std::cerr << "Build log:\n";
      std::cerr << program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(device) << "\n";
      return 3;
    }

    cl::Kernel kernel(program, "test_clock");

    std::vector<cl_ulong> out(12, 0);

    cl::Buffer outBuffer(context, CL_MEM_WRITE_ONLY,
                         sizeof(cl_ulong) * out.size());

    kernel.setArg(0, outBuffer);

    queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(1),
                               cl::NDRange(1));

    queue.finish();

    queue.enqueueReadBuffer(outBuffer, CL_TRUE, 0,
                            sizeof(cl_ulong) * out.size(), out.data());

    std::cout << "\n=== cl_khr_kernel_clock result ===\n\n";

    printPair("clock_read_device()", out[0], out[1]);
    printPair("clock_read_hilo_device()", out[2], out[3]);

    printPair("clock_read_work_group()", out[4], out[5]);
    printPair("clock_read_hilo_work_group()", out[6], out[7]);

    printPair("clock_read_sub_group()", out[8], out[9]);
    printPair("clock_read_hilo_sub_group()", out[10], out[11]);

    bool pass = out[1] >= out[0] && out[3] >= out[2] && out[5] >= out[4] &&
                out[7] >= out[6] && out[9] >= out[8] && out[11] >= out[10];

    std::cout << "\nOverall: " << (pass ? "PASS" : "FAIL") << "\n";

    return pass ? 0 : 4;

  } catch (const cl::Error &err) {
    std::cerr << "OpenCL error: " << err.what() << " (" << err.err() << ")\n";
    return 10;

  } catch (const std::exception &err) {
    std::cerr << "Error: " << err.what() << "\n";
    return 11;
  }
}
