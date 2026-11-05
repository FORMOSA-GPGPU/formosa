// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

// The kernel must fail with work-group 0's trap, although the work-groups
// retiring after it complete normally.

#include <unistd.h>

#include <CL/opencl.hpp>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef KERNEL_PATH
#define KERNEL_PATH "./kernel.cl"
#endif

namespace {

constexpr size_t kLocalSize = 16;
constexpr size_t kNumGroups = 8;
constexpr size_t kGlobalSize = kLocalSize * kNumGroups;
constexpr cl_uint kMisalignment = 1;
constexpr cl_uint kRounds = 64;
constexpr unsigned long kStoreAddressMisaligned = 6;

constexpr const char *kRed = "\033[0;31m";
constexpr const char *kGreen = "\033[0;32m";
constexpr const char *kReset = "\033[0m";

std::string loadFile(const std::string &path) {
  std::ifstream file(path);
  if (!file) {
    throw std::runtime_error("failed to open kernel source: " + path);
  }

  return {std::istreambuf_iterator<char>(file),
          std::istreambuf_iterator<char>()};
}

// Captures stderr until Finish. PoCL logs a failed kernel's trap only there.
class StderrCapture {
 public:
  StderrCapture() : file_(std::tmpfile()) {
    if (!file_) {
      throw std::runtime_error("failed to create a file for stderr");
    }
    std::fflush(stderr);
    saved_ = dup(STDERR_FILENO);
    if (saved_ < 0 || dup2(fileno(file_), STDERR_FILENO) < 0) {
      throw std::runtime_error("failed to capture stderr");
    }
  }

  ~StderrCapture() {
    Restore();
    std::fclose(file_);
  }

  std::string Finish() {
    Restore();
    std::string text;
    char buffer[256];
    std::rewind(file_);
    while (size_t size = std::fread(buffer, 1, sizeof(buffer), file_)) {
      text.append(buffer, size);
    }
    return text;
  }

 private:
  void Restore() {
    if (saved_ < 0) {
      return;
    }
    std::fflush(stderr);
    dup2(saved_, STDERR_FILENO);
    close(saved_);
    saved_ = -1;
  }

  std::FILE *file_;
  int saved_ = -1;
};

// The mcause PoCL logs for a kernel that completed with an exception.
bool findExceptionCause(const std::string &log, unsigned long *mcause) {
  const size_t exception = log.find("Exception occurs");
  if (exception == std::string::npos) {
    return false;
  }
  const size_t field = log.find("mcause:", exception);
  return field != std::string::npos &&
         std::sscanf(log.c_str() + field, "mcause: %lx", mcause) == 1;
}

}  // namespace

int main() {
  // PoCL reads this when it initializes; keep a level the user chose.
  setenv("POCL_DEBUG", "err", 0);

  try {
    std::vector<cl::Platform> platforms;
    cl::Platform::get(&platforms);
    if (platforms.empty()) {
      std::cerr << "No OpenCL platforms found" << std::endl;
      return 1;
    }

    std::vector<cl::Device> devices;
    platforms[0].getDevices(CL_DEVICE_TYPE_GPU, &devices);
    if (devices.empty()) {
      std::cerr << "No OpenCL GPU devices found" << std::endl;
      return 1;
    }

    cl::Device device = devices[0];
    cl::Context context({device});
    cl::CommandQueue queue(context, device);

    cl::Program program(context, loadFile(KERNEL_PATH));
    try {
      program.build({device});
    } catch (const cl::Error &) {
      std::cerr << "Build failed" << std::endl;
      std::cerr << program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(device)
                << std::endl;
      return 1;
    }

    cl::Buffer out(context, CL_MEM_WRITE_ONLY, sizeof(cl_uint) * kGlobalSize);
    cl::Kernel kernel(program, "trap_first_group");
    kernel.setArg(0, out);
    kernel.setArg(1, kMisalignment);
    kernel.setArg(2, kRounds);

    cl::Event event;
    std::string log;
    {
      StderrCapture capture;
      queue.enqueueNDRangeKernel(kernel, cl::NullRange,
                                 cl::NDRange(kGlobalSize),
                                 cl::NDRange(kLocalSize), nullptr, &event);
      try {
        queue.finish();
      } catch (const cl::Error &) {
        // The event status below tells whether the kernel failed.
      }
      log = capture.Finish();
    }
    std::cerr << log;

    bool pass = true;
    const cl_int status = event.getInfo<CL_EVENT_COMMAND_EXECUTION_STATUS>();
    if (status >= 0) {
      std::cerr << "The kernel completed although work-group 0 trapped"
                << std::endl;
      pass = false;
    }

    unsigned long mcause = 0;
    if (!findExceptionCause(log, &mcause)) {
      std::cerr << "PoCL reported no exception for the kernel" << std::endl;
      pass = false;
    } else if (mcause != kStoreAddressMisaligned) {
      std::cerr << "Expected mcause " << kStoreAddressMisaligned << " but got "
                << mcause << std::endl;
      pass = false;
    }

    if (pass) {
      std::cout << kGreen << "kernel_trap passed" << kReset << std::endl;
      return 0;
    }

    std::cout << kRed << "kernel_trap failed" << kReset << std::endl;
    return 1;
  } catch (const cl::Error &err) {
    std::cerr << "OpenCL error: " << err.what() << " (" << err.err() << ")"
              << std::endl;
    return 1;
  } catch (const std::exception &err) {
    std::cerr << "Error: " << err.what() << std::endl;
    return 1;
  }
}
