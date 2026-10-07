// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <CL/opencl.hpp>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr size_t kGlobalSize = 32;
constexpr size_t kLocalSize = 8;
constexpr size_t kMixedFields = 14;

const char *kSource = R"CLC(
typedef struct { float value; } Single;
typedef struct { float values[3]; } Triple;
typedef struct { uchar tag; ulong count; float value; } Padded;
typedef struct { float4 values; float tail; } Wide;
typedef struct __attribute__((aligned(128))) { float value; } OverAligned;

__kernel void scalar(float value, __global float *out) {
  out[get_global_id(0)] = value;
}

__kernel void one(Single p, __global float *out) {
  out[get_global_id(0)] = p.value;
}

__kernel void mixed(uchar lead, Triple t, uint scale, __global float *out,
                    Padded p, Wide w, __local float *scratch) {
  size_t i = get_global_id(0);
  size_t lid = get_local_id(0);
  scratch[lid] = t.values[0] + (float)lid;
  barrier(CLK_LOCAL_MEM_FENCE);
  size_t base = i * 14;
  out[base + 0] = (float)lead;
  out[base + 1] = t.values[0];
  out[base + 2] = t.values[1];
  out[base + 3] = t.values[2];
  out[base + 4] = (float)scale;
  out[base + 5] = (float)p.tag;
  out[base + 6] = (float)p.count;
  out[base + 7] = p.value;
  out[base + 8] = w.values.x;
  out[base + 9] = w.values.y;
  out[base + 10] = w.values.z;
  out[base + 11] = w.values.w;
  out[base + 12] = w.tail;
  out[base + 13] = scratch[(lid + 1) % get_local_size(0)];
}

__kernel void private_copy(Single p, __global float *out) {
  size_t i = get_global_id(0);
  // Force a write to the by-value object. Each work-item must own its copy.
  __private volatile float *value = &p.value;
  *value += (float)i;
  out[i] = *value;
}

__kernel void overaligned(OverAligned p, __global float *out) {
  out[get_global_id(0)] = p.value;
}

__kernel void automatic_local(Single p, __global float *out, uint scale) {
  __local float scratch[8];
  size_t lid = get_local_id(0);
  scratch[lid] = p.value + (float)lid;
  barrier(CLK_LOCAL_MEM_FENCE);
  out[get_global_id(0)] =
      scratch[(lid + 1) % get_local_size(0)] + (float)scale;
}
)CLC";

struct Single {
  cl_float value;
};
struct Triple {
  std::array<cl_float, 3> values;
};
struct Padded {
  cl_uchar tag;
  cl_ulong count;
  cl_float value;
};
struct Wide {
  cl_float4 values;
  cl_float tail;
};
struct alignas(128) OverAligned {
  cl_float value;
};
static_assert(sizeof(Single) == 4);
static_assert(sizeof(Triple) == 12 && alignof(Triple) == 4);
static_assert(sizeof(Padded) == 24 && alignof(Padded) == 8);
static_assert(sizeof(Wide) == 32 && alignof(Wide) == 16);
static_assert(sizeof(OverAligned) == 128 && alignof(OverAligned) == 128);

void run_and_check(cl::CommandQueue &queue, cl::Kernel &kernel, cl::Buffer &out,
                   const std::vector<float> &expected,
                   const std::string &name) {
  std::vector<float> actual(expected.size(), -999.0f);
  queue.enqueueWriteBuffer(out, CL_TRUE, 0, actual.size() * sizeof(float),
                           actual.data());
  queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(kGlobalSize),
                             cl::NDRange(kLocalSize));
  queue.finish();
  queue.enqueueReadBuffer(out, CL_TRUE, 0, actual.size() * sizeof(float),
                          actual.data());
  for (size_t i = 0; i < expected.size(); ++i) {
    if (actual[i] != expected[i]) {
      std::cerr << name << '[' << i << "]: got " << actual[i] << ", expected "
                << expected[i] << '\n';
      throw std::runtime_error("kernel argument result mismatch");
    }
  }
  std::cout << name << " passed\n";
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
    cl::Buffer out(context, CL_MEM_READ_WRITE,
                   kGlobalSize * kMixedFields * sizeof(float));
    const Single single{0.5f};
    cl::Kernel scalar(program, "scalar");
    scalar.setArg(0, single.value);
    scalar.setArg(1, out);
    run_and_check(queue, scalar, out, std::vector<float>(kGlobalSize, 0.5f),
                  "scalar");

    cl::Kernel one(program, "one");
    one.setArg(0, sizeof(single), &single);
    one.setArg(1, out);
    run_and_check(queue, one, out, std::vector<float>(kGlobalSize, 0.5f),
                  "single-float struct");

    const cl_uchar lead = 7;
    const Triple triple{{1.0f, 2.0f, 3.0f}};
    const cl_uint scale = 9;
    Padded padded{};
    padded.tag = 5;
    padded.count = 11;
    padded.value = 12.0f;
    Wide wide{};
    for (size_t i = 0; i < 4; ++i) wide.values.s[i] = 4.0f + float(i);
    wide.tail = 8.0f;
    cl::Kernel mixed(program, "mixed");
    mixed.setArg(0, lead);
    mixed.setArg(1, sizeof(triple), &triple);
    mixed.setArg(2, scale);
    mixed.setArg(3, out);
    mixed.setArg(4, sizeof(padded), &padded);
    mixed.setArg(5, sizeof(wide), &wide);
    mixed.setArg(6, cl::Local(kLocalSize * sizeof(float)));
    std::vector<float> expected(kGlobalSize * kMixedFields);
    const std::array<float, 13> fields = {7,  1, 2, 3, 9, 5, 11,
                                          12, 4, 5, 6, 7, 8};
    for (size_t i = 0; i < kGlobalSize; ++i) {
      for (size_t j = 0; j < fields.size(); ++j)
        expected[i * kMixedFields + j] = fields[j];
      expected[i * kMixedFields + 13] =
          1.0f + float((i % kLocalSize + 1) % kLocalSize);
    }
    run_and_check(queue, mixed, out, expected, "mixed argument layout");

    cl::Kernel private_copy(program, "private_copy");
    private_copy.setArg(0, sizeof(single), &single);
    private_copy.setArg(1, out);
    expected.resize(kGlobalSize);
    for (size_t i = 0; i < kGlobalSize; ++i) expected[i] = 0.5f + float(i);
    run_and_check(queue, private_copy, out, expected, "private struct copies");

    // Keep a cache-line-sized allocation live so the argument allocation
    // cannot rely on the natural alignment of the preceding output buffer.
    std::array<float, 16> guard{};
    cl::Buffer guard_buffer(context, CL_MEM_READ_WRITE, sizeof(guard));
    queue.enqueueWriteBuffer(guard_buffer, CL_TRUE, 0, sizeof(guard),
                             guard.data());
    cl::Kernel overaligned(program, "overaligned");
    const OverAligned aligned{0.75f};
    overaligned.setArg(0, sizeof(aligned), &aligned);
    overaligned.setArg(1, out);
    run_and_check(queue, overaligned, out,
                  std::vector<float>(kGlobalSize, 0.75f),
                  "overaligned struct allocation");

    cl::Kernel automatic_local(program, "automatic_local");
    automatic_local.setArg(0, sizeof(single), &single);
    automatic_local.setArg(1, out);
    automatic_local.setArg(2, scale);
    for (size_t i = 0; i < kGlobalSize; ++i)
      expected[i] = 0.5f + float((i % kLocalSize + 1) % kLocalSize) + scale;
    run_and_check(queue, automatic_local, out, expected,
                  "struct with automatic local memory");
    return 0;
  } catch (const cl::Error &error) {
    std::cerr << error.what() << " failed: " << error.err() << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
  }
  return 1;
}
