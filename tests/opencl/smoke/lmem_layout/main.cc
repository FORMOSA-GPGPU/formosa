// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

// Local memory of a work-group: every block starts at a multiple of 128 bytes,
// the alignment PoCL's Formosa launcher gives each local block, and no two
// blocks overlap. A block that is not aligned makes the first misaligned
// access trap; a block placed with a wrong size overlaps another block. The
// total that counts the padding is checked by smoke.lmem_size.

#include <CL/opencl.hpp>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr size_t kLocalSize = 8;
constexpr size_t kNumGroups = 8;
constexpr size_t kNumBlocks = 7;
// Not a multiple of 4: the float block that follows needs padding.
constexpr size_t kHostBytes = 133;
constexpr size_t kHostFloats = 36;

// Explicit local arguments first, then automatic locals of different types
// whose sizes neither grow nor shrink in declaration order. Blocks larger than
// 128 bytes overlap their successor when placed with a smaller size.
const char *kSource = R"CLC(
uint value(uint group, uint block, uint k) {
  return group * 7919u + block * 1543u + k * 31u + 1u;
}

#define FILL(p, n, type, block)                                              \
  for (uint k = lid; k < (n); k += lsize)                                    \
    (p)[k] = (type)(value(group, block, k) & 0xffffu)
#define CHECK(p, n, type, block)                                             \
  for (uint k = 0; k < (n); ++k)                                             \
    bad += (p)[k] != (type)(value(group, block, k) & 0xffffu)

__kernel void lmem_layout(__global uint *errors, __global uint *residues,
                          __local uchar *host_bytes,
                          __local float *host_floats) {
  __local float a[40];
  __local uchar b[3];
  __local float c[50];
  __local short d[70];
  __local ulong e[20];
  uint group = get_group_id(0), lid = get_local_id(0);
  uint lsize = get_local_size(0);
  FILL(host_bytes, HOST_BYTES, uchar, 0);
  FILL(host_floats, HOST_FLOATS, float, 1);
  FILL(a, 40, float, 2);
  FILL(b, 3, uchar, 3);
  FILL(c, 50, float, 4);
  FILL(d, 70, short, 5);
  FILL(e, 20, ulong, 6);
  barrier(CLK_LOCAL_MEM_FENCE);
  uint bad = 0;
  CHECK(host_bytes, HOST_BYTES, uchar, 0);
  CHECK(host_floats, HOST_FLOATS, float, 1);
  CHECK(a, 40, float, 2);
  CHECK(b, 3, uchar, 3);
  CHECK(c, 50, float, 4);
  CHECK(d, 70, short, 5);
  CHECK(e, 20, ulong, 6);
  errors[get_global_id(0)] = bad;
  if (lid == 0) {
    __global uint *r = residues + group * 7;
    r[0] = (uint)((uintptr_t)host_bytes % 128);
    r[1] = (uint)((uintptr_t)host_floats % 128);
    r[2] = (uint)((uintptr_t)a % 128);
    r[3] = (uint)((uintptr_t)b % 128);
    r[4] = (uint)((uintptr_t)c % 128);
    r[5] = (uint)((uintptr_t)d % 128);
    r[6] = (uint)((uintptr_t)e % 128);
  }
}
)CLC";

const std::array<const char *, kNumBlocks> kBlockNames = {
    "host_bytes", "host_floats", "a", "b", "c", "d", "e"};

}  // namespace

int main() {
  try {
    std::vector<cl::Platform> platforms;
    cl::Platform::get(&platforms);
    if (platforms.empty()) throw std::runtime_error("no OpenCL platform");
    std::vector<cl::Device> devices;
    platforms.front().getDevices(CL_DEVICE_TYPE_GPU, &devices);
    if (devices.empty()) throw std::runtime_error("no OpenCL GPU");
    const auto &device = devices.front();
    cl::Context context(device);
    cl::CommandQueue queue(context, device);
    cl::Program program(context, kSource);
    const std::string options =
        "-cl-std=CL1.2 -DHOST_BYTES=" + std::to_string(kHostBytes) +
        " -DHOST_FLOATS=" + std::to_string(kHostFloats);
    try {
      program.build({device}, options.c_str());
    } catch (const cl::Error &) {
      std::cerr << program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(device) << '\n';
      throw;
    }

    // Anything the kernel does not overwrite reads as a failure.
    std::vector<cl_uint> errors(kNumGroups * kLocalSize, ~0u);
    std::vector<cl_uint> residues(kNumGroups * kNumBlocks, ~0u);
    cl::Buffer errors_buffer(context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                             errors.size() * sizeof(cl_uint), errors.data());
    cl::Buffer residues_buffer(
        context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
        residues.size() * sizeof(cl_uint), residues.data());
    cl::Kernel kernel(program, "lmem_layout");
    kernel.setArg(0, errors_buffer);
    kernel.setArg(1, residues_buffer);
    kernel.setArg(2, cl::Local(kHostBytes));
    kernel.setArg(3, cl::Local(kHostFloats * sizeof(cl_float)));

    cl::Event run;
    queue.enqueueNDRangeKernel(kernel, cl::NullRange,
                               cl::NDRange(kNumGroups * kLocalSize),
                               cl::NDRange(kLocalSize), nullptr, &run);
    queue.finish();
    // A misaligned local access traps and fails the kernel's event.
    const cl_int status = run.getInfo<CL_EVENT_COMMAND_EXECUTION_STATUS>();
    if (status != CL_COMPLETE)
      throw std::runtime_error("kernel execution failed: " +
                               std::to_string(status));
    queue.enqueueReadBuffer(errors_buffer, CL_TRUE, 0,
                            errors.size() * sizeof(cl_uint), errors.data());
    queue.enqueueReadBuffer(residues_buffer, CL_TRUE, 0,
                            residues.size() * sizeof(cl_uint), residues.data());

    bool pass = true;
    for (size_t i = 0; i < errors.size(); ++i) {
      if (errors[i] == 0) continue;
      std::cerr << "work-item " << i << " found " << errors[i]
                << " wrong local values\n";
      pass = false;
    }
    for (size_t i = 0; i < residues.size(); ++i) {
      if (residues[i] == 0) continue;
      std::cerr << "group " << i / kNumBlocks << ": block "
                << kBlockNames[i % kNumBlocks] << " is at " << residues[i]
                << " modulo 128\n";
      pass = false;
    }
    if (!pass) throw std::runtime_error("local memory layout mismatch");
    std::cout << "lmem_layout passed\n";
    return 0;
  } catch (const cl::Error &error) {
    std::cerr << error.what() << " failed: " << error.err() << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
  }
  return 1;
}
