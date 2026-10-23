// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

// Local memory a launch uses: every local block starts at a multiple of 128
// bytes (see smoke.lmem_layout), so the padding counts.
// CL_KERNEL_LOCAL_MEM_SIZE must not report less than a launch uses, and a
// launch whose padded local memory exceeds CL_DEVICE_LOCAL_MEM_SIZE fails with
// CL_OUT_OF_RESOURCES even when the sizes without the padding fit.

#include <CL/opencl.hpp>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr size_t kAlign = 128;
constexpr size_t kLocalSize = 8;

size_t align_up(size_t value) { return (value + kAlign - 1) / kAlign * kAlign; }

// autos: automatic locals only. mixed: an explicit local, then an automatic
// one. two: two explicit locals.
const char *kSource = R"CLC(
__kernel void autos(__global uint *out) {
  __local uchar a[3];
  __local uchar b[3];
  uint l = get_local_id(0);
  if (l < 3) {
    a[l] = l;
    b[l] = l + 1;
  }
  barrier(CLK_LOCAL_MEM_FENCE);
  out[get_global_id(0)] = a[l % 3] + b[l % 3];
}

__kernel void mixed(__global uint *out, __local uchar *x) {
  __local uchar a[3];
  uint l = get_local_id(0);
  x[l] = l;
  if (l < 3) a[l] = l;
  barrier(CLK_LOCAL_MEM_FENCE);
  out[get_global_id(0)] = x[l] + a[l % 3];
}

__kernel void two(__global uint *out, __local uchar *x, __local float *y) {
  uint l = get_local_id(0);
  x[l] = l;
  y[l] = l;
  barrier(CLK_LOCAL_MEM_FENCE);
  out[get_global_id(0)] = x[l] + (uint)y[l];
}
)CLC";

// An implementation may refuse the launch at enqueue or fail its event.
cl_int run(cl::CommandQueue &queue, cl::Kernel &kernel) {
  cl::Event event;
  try {
    queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(kLocalSize),
                               cl::NDRange(kLocalSize), nullptr, &event);
  } catch (const cl::Error &error) {
    return error.err();
  }
  queue.finish();
  return event.getInfo<CL_EVENT_COMMAND_EXECUTION_STATUS>();
}

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
    try {
      program.build({device}, "-cl-std=CL1.2");
    } catch (const cl::Error &) {
      std::cerr << program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(device) << '\n';
      throw;
    }
    cl::Buffer out(context, CL_MEM_READ_WRITE, kLocalSize * sizeof(cl_uint));

    bool pass = true;
    auto check = [&](const char *name, cl::Kernel &kernel, size_t uses,
                     cl_int want) {
      const cl_ulong reported =
          kernel.getWorkGroupInfo<CL_KERNEL_LOCAL_MEM_SIZE>(device);
      if (reported < uses) {
        std::cerr << name << ": CL_KERNEL_LOCAL_MEM_SIZE is " << reported
                  << ", the launch uses " << uses << '\n';
        pass = false;
      }
      const cl_int status = run(queue, kernel);
      if (status != want) {
        std::cerr << name << ": launch status " << status << ", want " << want
                  << '\n';
        pass = false;
      }
    };

    cl::Kernel autos(program, "autos");
    autos.setArg(0, out);
    check("autos", autos, align_up(3) + 3, CL_COMPLETE);

    cl::Kernel mixed(program, "mixed");
    mixed.setArg(0, out);
    mixed.setArg(1, cl::Local(133));
    check("mixed", mixed, align_up(133) + 3, CL_COMPLETE);

    cl::Kernel two(program, "two");
    two.setArg(0, out);
    two.setArg(1, cl::Local(133));
    two.setArg(2, cl::Local(36 * sizeof(cl_float)));
    check("two", two, align_up(133) + 36 * sizeof(cl_float), CL_COMPLETE);

    // Without the padding the two blocks fit the device; with it they do not.
    const size_t device_local = device.getInfo<CL_DEVICE_LOCAL_MEM_SIZE>();
    const size_t bytes = device_local / 2 / kAlign * kAlign + 1;
    const size_t floats = (device_local - bytes) / sizeof(cl_float);
    const size_t padded = align_up(bytes) + floats * sizeof(cl_float);
    if (bytes + floats * sizeof(cl_float) > device_local ||
        padded <= device_local)
      throw std::runtime_error("cannot size the local blocks for " +
                               std::to_string(device_local) + " bytes");
    cl::Kernel overflow(program, "two");
    overflow.setArg(0, out);
    overflow.setArg(1, cl::Local(bytes));
    overflow.setArg(2, cl::Local(floats * sizeof(cl_float)));
    check("overflow", overflow, padded, CL_OUT_OF_RESOURCES);

    if (!pass) throw std::runtime_error("local memory size mismatch");
    std::cout << "lmem_size passed\n";
    return 0;
  } catch (const cl::Error &error) {
    std::cerr << error.what() << " failed: " << error.err() << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
  }
  return 1;
}
