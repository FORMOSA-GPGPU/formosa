// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <CL/opencl.hpp>
#include <algorithm>
#include <array>
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

constexpr size_t kGlobalSize = 4;
constexpr size_t kLocalSize = 1;
constexpr size_t kMaxRecords = 32;

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

void printRecords(int counter, const std::vector<int> &gid_out,
                  const std::vector<int> &lid_out,
                  const std::vector<int> &group_out) {
  std::cout << "counter = " << counter << std::endl;

  const int records_to_print =
      std::min<int>(counter, static_cast<int>(gid_out.size()));
  for (int i = 0; i < records_to_print; ++i) {
    std::cout << "slot " << i << ": gid=" << gid_out[i] << " lid=" << lid_out[i]
              << " group=" << group_out[i] << std::endl;
  }
}

bool validateResults(int counter, const std::vector<int> &gid_out,
                     const std::vector<int> &lid_out,
                     const std::vector<int> &group_out) {
  bool pass = true;

  if (counter != static_cast<int>(kGlobalSize)) {
    std::cerr << "Expected counter " << kGlobalSize << " but got " << counter
              << std::endl;
    pass = false;
  }

  std::array<bool, kGlobalSize> seen_gid = {};
  std::array<bool, kGlobalSize> seen_group = {};
  const int valid_records =
      std::min<int>(counter, static_cast<int>(kGlobalSize));

  for (int i = 0; i < valid_records; ++i) {
    const int gid = gid_out[i];
    const int lid = lid_out[i];
    const int group = group_out[i];

    if (gid < 0 || gid >= static_cast<int>(kGlobalSize)) {
      std::cerr << "Invalid gid at slot " << i << ": " << gid << std::endl;
      pass = false;
    } else if (seen_gid[gid]) {
      std::cerr << "Duplicate gid at slot " << i << ": " << gid << std::endl;
      pass = false;
    } else {
      seen_gid[gid] = true;
    }

    if (lid != 0) {
      std::cerr << "Expected lid 0 at slot " << i << " but got " << lid
                << std::endl;
      pass = false;
    }

    if (group < 0 || group >= static_cast<int>(kGlobalSize)) {
      std::cerr << "Invalid group at slot " << i << ": " << group << std::endl;
      pass = false;
    } else if (seen_group[group]) {
      std::cerr << "Duplicate group at slot " << i << ": " << group
                << std::endl;
      pass = false;
    } else {
      seen_group[group] = true;
    }
  }

  for (size_t i = 0; i < kGlobalSize; ++i) {
    if (!seen_gid[i]) {
      std::cerr << "Missing gid " << i << std::endl;
      pass = false;
    }
    if (!seen_group[i]) {
      std::cerr << "Missing group " << i << std::endl;
      pass = false;
    }
  }

  if (!pass) {
    printRecords(counter, gid_out, lid_out, group_out);
  }

  return pass;
}

}  // namespace

int main() {
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
    std::cout << "Device: " << device.getInfo<CL_DEVICE_NAME>() << std::endl;
    std::cout << "Kernel Path: " << KERNEL_PATH << std::endl;

    cl::Context context({device});
    cl::CommandQueue queue(context, device);

    const std::string source = loadFile(KERNEL_PATH);
    cl::Program program(context, source);
    try {
      program.build({device});
    } catch (const cl::Error &) {
      std::cerr << "Build failed" << std::endl;
      std::cerr << program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(device)
                << std::endl;
      return 1;
    }

    int counter = 0;
    std::vector<int> gid_out(kMaxRecords, -1);
    std::vector<int> lid_out(kMaxRecords, -1);
    std::vector<int> group_out(kMaxRecords, -1);

    cl::Buffer counter_buf(context, CL_MEM_READ_WRITE, sizeof(counter));
    cl::Buffer gid_buf(context, CL_MEM_READ_WRITE,
                       sizeof(int) * gid_out.size());
    cl::Buffer lid_buf(context, CL_MEM_READ_WRITE,
                       sizeof(int) * lid_out.size());
    cl::Buffer group_buf(context, CL_MEM_READ_WRITE,
                         sizeof(int) * group_out.size());

    queue.enqueueWriteBuffer(counter_buf, CL_FALSE, 0, sizeof(counter),
                             &counter);
    queue.enqueueWriteBuffer(gid_buf, CL_FALSE, 0, sizeof(int) * gid_out.size(),
                             gid_out.data());
    queue.enqueueWriteBuffer(lid_buf, CL_FALSE, 0, sizeof(int) * lid_out.size(),
                             lid_out.data());
    queue.enqueueWriteBuffer(group_buf, CL_FALSE, 0,
                             sizeof(int) * group_out.size(), group_out.data());

    cl::Kernel kernel(program, "test_inactive_lanes");
    kernel.setArg(0, counter_buf);
    kernel.setArg(1, gid_buf);
    kernel.setArg(2, lid_buf);
    kernel.setArg(3, group_buf);

    queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(kGlobalSize),
                               cl::NDRange(kLocalSize));
    queue.finish();

    queue.enqueueReadBuffer(counter_buf, CL_TRUE, 0, sizeof(counter), &counter);
    queue.enqueueReadBuffer(gid_buf, CL_TRUE, 0, sizeof(int) * gid_out.size(),
                            gid_out.data());
    queue.enqueueReadBuffer(lid_buf, CL_TRUE, 0, sizeof(int) * lid_out.size(),
                            lid_out.data());
    queue.enqueueReadBuffer(group_buf, CL_TRUE, 0,
                            sizeof(int) * group_out.size(), group_out.data());

    const bool pass = validateResults(counter, gid_out, lid_out, group_out);

    if (pass) {
      std::cout << kGreen << "inactive_lanes passed" << kReset << std::endl;
      return 0;
    }

    std::cout << kRed << "inactive_lanes failed" << kReset << std::endl;
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
