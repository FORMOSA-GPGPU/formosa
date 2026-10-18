// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <CL/opencl.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <type_traits>
#include <vector>

#ifndef KERNEL_PATH
#define KERNEL_PATH "./kernel.cl"
#endif

namespace {

constexpr size_t kElements = 256;
constexpr size_t kLocalSize = 64;
constexpr size_t kLocalElements = kLocalSize;
constexpr size_t kC11Groups = 2;

constexpr const char *kRed = "\033[0;31m";
constexpr const char *kGreen = "\033[0;32m";
constexpr const char *kReset = "\033[0m";

enum class Operation : size_t {
  Add = 0,
  Sub,
  Inc,
  Dec,
  And,
  Or,
  Xor,
  Xchg,
  Min,
  Max,
  Cmpxchg,
  Count,
};

constexpr size_t kOperationCount = static_cast<size_t>(Operation::Count);

constexpr std::array<const char *, kOperationCount> kOperationNames = {
    "add", "sub",  "inc", "dec", "and",     "or",
    "xor", "xchg", "min", "max", "cmpxchg",
};

template <typename T>
constexpr auto PrintValue(T value) {
  if constexpr (std::is_signed_v<T>) {
    return static_cast<long long>(value);
  } else {
    return static_cast<unsigned long long>(value);
  }
}

constexpr size_t FlatIndex(Operation op, size_t element_count, size_t gid) {
  return static_cast<size_t>(op) * element_count + gid;
}

template <typename T>
T MakeInitialValue(Operation op, size_t gid) {
  if (op == Operation::Cmpxchg) {
    using U = std::make_unsigned_t<T>;
    return static_cast<T>((U{1} << (sizeof(T) * 8 - 1)) |
                          static_cast<U>(gid + 64));
  }
  if constexpr (std::is_signed_v<T>) {
    switch (op) {
      case Operation::Add:
        return static_cast<T>(gid + 17);
      case Operation::Sub:
        return static_cast<T>(gid + 200);
      case Operation::Inc:
        return static_cast<T>(gid + 300);
      case Operation::Dec:
        return static_cast<T>(gid + 400);
      case Operation::And:
        return static_cast<T>(0x7f0 + gid);
      case Operation::Or:
        return static_cast<T>(0x120 + gid);
      case Operation::Xor:
        return static_cast<T>(0x550 + gid);
      case Operation::Xchg:
        return static_cast<T>(gid - 90);
      case Operation::Min:
        return static_cast<T>(static_cast<long long>(gid) - 80);
      case Operation::Max:
        return static_cast<T>(static_cast<long long>(gid) - 40);
      case Operation::Cmpxchg:
      case Operation::Count:
        break;
    }
  } else {
    switch (op) {
      case Operation::Add:
        return static_cast<T>(gid * 3 + 17);
      case Operation::Sub:
        return static_cast<T>(gid * 5 + 200);
      case Operation::Inc:
        return static_cast<T>(gid + 300);
      case Operation::Dec:
        return static_cast<T>(gid + 400);
      case Operation::And:
        return static_cast<T>(0x7f0u + gid);
      case Operation::Or:
        return static_cast<T>(0x120u + gid);
      case Operation::Xor:
        return static_cast<T>(0x550u + gid);
      case Operation::Xchg:
        return static_cast<T>(gid + 600);
      case Operation::Min:
        return static_cast<T>(gid + 900);
      case Operation::Max:
        return static_cast<T>(gid + 1100);
      case Operation::Cmpxchg:
      case Operation::Count:
        break;
    }
  }

  return static_cast<T>(0);
}

template <typename T>
T MakeOperandValue(Operation op, size_t gid, T initial) {
  switch (op) {
    case Operation::Add:
      return static_cast<T>((gid % 9) + 1);
    case Operation::Sub:
      return static_cast<T>((gid % 7) + 1);
    case Operation::Inc:
    case Operation::Dec:
      return static_cast<T>(0);
    case Operation::And:
      return static_cast<T>((gid * 13) ^ 0x3f3u);
    case Operation::Or:
      return static_cast<T>((gid * 7) | 0x080u);
    case Operation::Xor:
      return static_cast<T>((gid * 11) ^ 0x055u);
    case Operation::Cmpxchg:
    case Operation::Xchg:
      if constexpr (std::is_signed_v<T>) {
        return static_cast<T>(500 - static_cast<long long>(gid));
      } else {
        return static_cast<T>(1500 + gid);
      }
    case Operation::Min:
      if constexpr (std::is_signed_v<T>) {
        return static_cast<T>(initial + ((gid % 2 == 0) ? -5 : 5));
      } else {
        return static_cast<T>(initial + ((gid % 2 == 0) ? -5 : 5));
      }
    case Operation::Max:
      if constexpr (std::is_signed_v<T>) {
        return static_cast<T>(initial + ((gid % 2 == 0) ? 7 : -7));
      } else {
        return static_cast<T>(initial + ((gid % 2 == 0) ? 7 : -7));
      }
    case Operation::Count:
      break;
  }

  return static_cast<T>(0);
}

template <typename T>
T ApplyExpected(Operation op, T initial, T operand, size_t gid) {
  switch (op) {
    case Operation::Add:
      return static_cast<T>(initial + operand);
    case Operation::Sub:
      return static_cast<T>(initial - operand);
    case Operation::Inc:
      return static_cast<T>(initial + 1);
    case Operation::Dec:
      return static_cast<T>(initial - 1);
    case Operation::And:
      return static_cast<T>(initial & operand);
    case Operation::Or:
      return static_cast<T>(initial | operand);
    case Operation::Xor:
      return static_cast<T>(initial ^ operand);
    case Operation::Xchg:
      return operand;
    case Operation::Cmpxchg:
      return (gid & 1) ? initial : operand;
    case Operation::Min:
      if constexpr (std::is_signed_v<T>) {
        return std::min(initial, operand);
      } else {
        return std::min(initial, operand);
      }
    case Operation::Max:
      if constexpr (std::is_signed_v<T>) {
        return std::max(initial, operand);
      } else {
        return std::max(initial, operand);
      }
    case Operation::Count:
      break;
  }

  return initial;
}

template <typename T>
const char *GlobalOpsKernelName();

template <>
const char *GlobalOpsKernelName<cl_int>() {
  return "atomic_ops_global_i32";
}

template <>
const char *GlobalOpsKernelName<cl_uint>() {
  return "atomic_ops_global_u32";
}

template <>
const char *GlobalOpsKernelName<cl_long>() {
  return "atomic_ops_global_i64";
}

template <>
const char *GlobalOpsKernelName<cl_ulong>() {
  return "atomic_ops_global_u64";
}

template <typename T>
const char *ContendedGlobalAddKernelName();

template <>
const char *ContendedGlobalAddKernelName<cl_uint>() {
  return "atomic_contended_global_u32";
}

template <>
const char *ContendedGlobalAddKernelName<cl_ulong>() {
  return "atomic_contended_global_u64";
}

template <typename T>
const char *LocalOpsKernelName();

template <>
const char *LocalOpsKernelName<cl_int>() {
  return "atomic_ops_local_i32";
}

template <>
const char *LocalOpsKernelName<cl_uint>() {
  return "atomic_ops_local_u32";
}

template <>
const char *LocalOpsKernelName<cl_long>() {
  return "atomic_ops_local_i64";
}

template <>
const char *LocalOpsKernelName<cl_ulong>() {
  return "atomic_ops_local_u64";
}

template <typename T>
const char *ContendedLocalAddKernelName();

template <>
const char *ContendedLocalAddKernelName<cl_uint>() {
  return "atomic_contended_local_u32";
}

template <>
const char *ContendedLocalAddKernelName<cl_ulong>() {
  return "atomic_contended_local_u64";
}

template <typename T>
bool CompareIndependentResults(const char *label, size_t element_count,
                               const std::vector<T> &initials,
                               const std::vector<T> &operands,
                               const std::vector<T> &old_values,
                               const std::vector<T> &final_values) {
  bool ok = true;
  size_t mismatches = 0;

  for (size_t op_index = 0; op_index < kOperationCount; ++op_index) {
    Operation op = static_cast<Operation>(op_index);
    for (size_t gid = 0; gid < element_count; ++gid) {
      size_t idx = FlatIndex(op, element_count, gid);
      T expected_old = initials[idx];
      T expected_final = ApplyExpected(op, initials[idx], operands[idx], gid);

      if (old_values[idx] != expected_old ||
          final_values[idx] != expected_final) {
        if (mismatches < 12) {
          std::cerr << kRed << label
                    << " mismatch op=" << kOperationNames[op_index]
                    << " gid=" << gid
                    << " old(expected=" << PrintValue(expected_old)
                    << ", got=" << PrintValue(old_values[idx])
                    << ") final(expected=" << PrintValue(expected_final)
                    << ", got=" << PrintValue(final_values[idx]) << ")"
                    << kReset << std::endl;
        }
        ++mismatches;
        ok = false;
      }
    }
  }

  if (!ok) {
    std::cerr << kRed << label << " failed with " << mismatches << " mismatches"
              << kReset << std::endl;
  }

  return ok;
}

template <typename T>
bool RunIndependentGlobalOpsTest(cl::Context &context, cl::CommandQueue &queue,
                                 cl::Program &program, const char *label) {
  const size_t total = kOperationCount * kElements;
  std::vector<T> initials(total);
  std::vector<T> operands(total);
  std::vector<T> old_values(total, 0);

  for (size_t op_index = 0; op_index < kOperationCount; ++op_index) {
    Operation op = static_cast<Operation>(op_index);
    for (size_t gid = 0; gid < kElements; ++gid) {
      size_t idx = FlatIndex(op, kElements, gid);
      initials[idx] = MakeInitialValue<T>(op, gid);
      operands[idx] = MakeOperandValue<T>(op, gid, initials[idx]);
    }
  }

  cl::Buffer values_buf(context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                        sizeof(T) * total, initials.data());
  cl::Buffer operands_buf(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                          sizeof(T) * total, operands.data());
  cl::Buffer old_values_buf(context, CL_MEM_READ_WRITE, sizeof(T) * total);

  cl::Kernel kernel(program, GlobalOpsKernelName<T>());
  kernel.setArg(0, values_buf);
  kernel.setArg(1, operands_buf);
  kernel.setArg(2, old_values_buf);

  queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(kElements),
                             cl::NDRange(kLocalSize));

  std::vector<T> final_values(total, 0);
  queue.enqueueReadBuffer(values_buf, CL_FALSE, 0, sizeof(T) * total,
                          final_values.data());
  queue.enqueueReadBuffer(old_values_buf, CL_FALSE, 0, sizeof(T) * total,
                          old_values.data());
  queue.finish();

  return CompareIndependentResults(label, kElements, initials, operands,
                                   old_values, final_values);
}

template <typename T>
bool CheckPermutation(const char *label, const std::vector<T> &values,
                      size_t begin, size_t count) {
  std::vector<uint8_t> seen(count, 0);
  bool ok = true;

  for (size_t i = 0; i < count; ++i) {
    T value = values[begin + i];
    if (value >= static_cast<T>(count)) {
      std::cerr << kRed << label << " out-of-range old value at index "
                << (begin + i) << ": " << PrintValue(value) << kReset
                << std::endl;
      ok = false;
      continue;
    }

    size_t slot = static_cast<size_t>(value);
    if (seen[slot] != 0) {
      std::cerr << kRed << label << " duplicate old value " << slot
                << " at index " << (begin + i) << kReset << std::endl;
      ok = false;
      continue;
    }
    seen[slot] = 1;
  }

  for (size_t i = 0; i < count; ++i) {
    if (seen[i] == 0) {
      std::cerr << kRed << label << " missing old value " << i << kReset
                << std::endl;
      ok = false;
    }
  }

  return ok;
}

template <typename T>
bool RunContendedGlobalAddTest(cl::Context &context, cl::CommandQueue &queue,
                               cl::Program &program, const char *label) {
  T counter = 0;
  std::vector<T> old_values(kElements, 0);

  cl::Buffer counter_buf(context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                         sizeof(counter), &counter);
  cl::Buffer old_values_buf(context, CL_MEM_READ_WRITE, sizeof(T) * kElements);

  cl::Kernel kernel(program, ContendedGlobalAddKernelName<T>());
  kernel.setArg(0, counter_buf);
  kernel.setArg(1, old_values_buf);

  queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(kElements),
                             cl::NDRange(kLocalSize));
  queue.enqueueReadBuffer(counter_buf, CL_FALSE, 0, sizeof(counter), &counter);
  queue.enqueueReadBuffer(old_values_buf, CL_FALSE, 0, sizeof(T) * kElements,
                          old_values.data());
  queue.finish();

  bool ok = true;
  if (counter != static_cast<T>(kElements)) {
    std::cerr << kRed << label << " final counter mismatch: expected "
              << kElements << " but got " << PrintValue(counter) << kReset
              << std::endl;
    ok = false;
  }

  return CheckPermutation(label, old_values, 0, kElements) && ok;
}

template <typename T>
bool RunIndependentLocalOpsTest(cl::Context &context, cl::CommandQueue &queue,
                                cl::Program &program, const char *label) {
  const size_t total = kOperationCount * kLocalElements;
  std::vector<T> initials(total);
  std::vector<T> operands(total);
  std::vector<T> old_values(total, 0);

  for (size_t op_index = 0; op_index < kOperationCount; ++op_index) {
    Operation op = static_cast<Operation>(op_index);
    for (size_t gid = 0; gid < kLocalElements; ++gid) {
      size_t idx = FlatIndex(op, kLocalElements, gid);
      initials[idx] = MakeInitialValue<T>(op, gid);
      operands[idx] = MakeOperandValue<T>(op, gid, initials[idx]);
    }
  }

  cl::Buffer initials_buf(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                          sizeof(T) * total, initials.data());
  cl::Buffer operands_buf(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                          sizeof(T) * total, operands.data());
  cl::Buffer final_values_buf(context, CL_MEM_READ_WRITE, sizeof(T) * total);
  cl::Buffer old_values_buf(context, CL_MEM_READ_WRITE, sizeof(T) * total);

  cl::Kernel kernel(program, LocalOpsKernelName<T>());
  kernel.setArg(0, initials_buf);
  kernel.setArg(1, operands_buf);
  kernel.setArg(2, final_values_buf);
  kernel.setArg(3, old_values_buf);

  queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(kLocalElements),
                             cl::NDRange(kLocalSize));

  std::vector<T> final_values(total, 0);
  queue.enqueueReadBuffer(final_values_buf, CL_FALSE, 0, sizeof(T) * total,
                          final_values.data());
  queue.enqueueReadBuffer(old_values_buf, CL_FALSE, 0, sizeof(T) * total,
                          old_values.data());
  queue.finish();

  return CompareIndependentResults(label, kLocalElements, initials, operands,
                                   old_values, final_values);
}

template <typename T>
bool RunContendedLocalAddTest(cl::Context &context, cl::CommandQueue &queue,
                              cl::Program &program, const char *label) {
  T counter = 0;
  std::vector<T> old_values(kLocalElements, 0);

  cl::Buffer counter_buf(context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                         sizeof(counter), &counter);
  cl::Buffer old_values_buf(context, CL_MEM_READ_WRITE,
                            sizeof(T) * kLocalElements);

  cl::Kernel kernel(program, ContendedLocalAddKernelName<T>());
  kernel.setArg(0, counter_buf);
  kernel.setArg(1, old_values_buf);

  queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(kLocalElements),
                             cl::NDRange(kLocalSize));
  queue.enqueueReadBuffer(counter_buf, CL_FALSE, 0, sizeof(counter), &counter);
  queue.enqueueReadBuffer(old_values_buf, CL_FALSE, 0,
                          sizeof(T) * kLocalElements, old_values.data());
  queue.finish();

  bool ok = true;
  if (counter != static_cast<T>(kLocalElements)) {
    std::cerr << kRed << label << " final counter mismatch: expected "
              << kLocalElements << " but got " << PrintValue(counter) << kReset
              << std::endl;
    ok = false;
  }

  return CheckPermutation(label, old_values, 0, kLocalElements) && ok;
}

bool RunC11Test(cl::Context &context, cl::CommandQueue &queue,
                const cl::Device &device, const char *options,
                const char *il_path = nullptr) {
  std::ifstream stream(il_path ? il_path : C11_KERNEL_PATH, std::ios::binary);
  if (!stream) return false;
  std::string source{std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>()};
  cl::Program program;
  if (il_path) {
    auto create = reinterpret_cast<clCreateProgramWithILKHR_fn>(
        clGetExtensionFunctionAddressForPlatform(
            device.getInfo<CL_DEVICE_PLATFORM>()(),
            "clCreateProgramWithILKHR"));
    if (!create) return false;
    cl_int error = CL_SUCCESS;
    cl_program raw = create(context(), source.data(), source.size(), &error);
    if (error != CL_SUCCESS || raw == nullptr) return false;
    program = cl::Program(raw);
  } else {
    program = cl::Program(context, source);
  }
  try {
    program.build(options);
  } catch (const cl::BuildError &) {
    std::cerr << program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(device)
              << std::endl;
    return false;
  }
  std::array<cl_ulong, 8> storage{};
  std::array<cl_uint, 3 * kC11Groups> result{};
  std::vector<cl_uint> old_values(2 * kLocalSize * kC11Groups);
  cl::Buffer storage_buf(context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                         sizeof(storage), storage.data());
  cl::Buffer result_buf(context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                        sizeof(result), result.data());
  cl::Buffer old_values_buf(context, CL_MEM_READ_WRITE,
                            sizeof(cl_uint) * old_values.size());
  bool passed = true;
  for (const char *name :
       {"c11_i32", "c11_u32", "c11_i64", "c11_u64", "c11_f32", "c11_f64"}) {
    result.fill(0xdeadbeef);
    queue.enqueueWriteBuffer(result_buf, CL_TRUE, 0, sizeof(result),
                             result.data());
    std::fill(old_values.begin(), old_values.end(), 0xdeadbeef);
    queue.enqueueWriteBuffer(old_values_buf, CL_TRUE, 0,
                             sizeof(cl_uint) * old_values.size(),
                             old_values.data());
    cl::Kernel kernel(program, name);
    kernel.setArg(0, storage_buf);
    kernel.setArg(1, result_buf);
    kernel.setArg(2, old_values_buf);
    queue.enqueueNDRangeKernel(kernel, cl::NullRange,
                               cl::NDRange(kLocalSize * kC11Groups),
                               cl::NDRange(kLocalSize));
    queue.enqueueReadBuffer(result_buf, CL_FALSE, 0, sizeof(result),
                            result.data());
    queue.enqueueReadBuffer(old_values_buf, CL_FALSE, 0,
                            sizeof(cl_uint) * old_values.size(),
                            old_values.data());
    queue.finish();
    for (size_t group = 0; group < kC11Groups; ++group) {
      const size_t offset = 3 * group;
      bool ok = result[offset] == 0 && result[offset + 1] == kLocalSize &&
                result[offset + 2] == kLocalSize;
      const size_t old_offset = 2 * kLocalSize * group;
      ok &= CheckPermutation("C11 global CAS", old_values, old_offset,
                             kLocalSize);
      ok &= CheckPermutation("C11 local CAS", old_values,
                             old_offset + kLocalSize, kLocalSize);
      std::cout << name << " " << options << " group=" << group
                << ": errors=" << result[offset]
                << " global=" << result[offset + 1]
                << " local=" << result[offset + 2] << std::endl;
      passed &= ok;
    }
  }
  result.fill(0xdeadbeef);
  queue.enqueueWriteBuffer(result_buf, CL_TRUE, 0, sizeof(result),
                           result.data());
  cl::Kernel flag(program, "c11_flag");
  flag.setArg(0, storage_buf);
  flag.setArg(1, result_buf);
  queue.enqueueNDRangeKernel(flag, cl::NullRange, cl::NDRange(1),
                             cl::NDRange(1));
  queue.enqueueReadBuffer(result_buf, CL_TRUE, 0, sizeof(cl_uint),
                          result.data());
  std::cout << "c11_flag " << options << ": errors=" << result[0] << std::endl;
  passed &= result[0] == 0;
  return passed;
}

}  // namespace

int main(int argc, char **argv) {
  std::vector<cl::Platform> platforms;
  cl::Platform::get(&platforms);
  if (platforms.empty()) {
    std::cerr << "No platforms!" << std::endl;
    return -1;
  }

  cl::Platform platform = platforms[0];
  std::vector<cl::Device> devices;
  platform.getDevices(CL_DEVICE_TYPE_GPU, &devices);
  if (devices.empty()) {
    std::cerr << "No devices!" << std::endl;
    return -1;
  }

  cl::Device device = devices[0];
  cl::Context context({device});
  cl::CommandQueue queue(context, device);

  if (argc == 2) {
    return RunC11Test(context, queue, device, "", argv[1]) ? 0 : 1;
  }

  std::ifstream kernel_stream(KERNEL_PATH);
  if (!kernel_stream) {
    std::cerr << "Error opening kernel source file" << std::endl;
    return -1;
  }

  std::string source{std::istreambuf_iterator<char>(kernel_stream),
                     std::istreambuf_iterator<char>()};
  cl::Program program(context, source);
  try {
    program.build();
  } catch (const cl::BuildError &) {
    std::cerr << program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(device)
              << std::endl;
    return -1;
  }

  bool ok = true;

  ok &= RunIndependentGlobalOpsTest<cl_int>(context, queue, program,
                                            "global_i32_ops");
  ok &= RunIndependentGlobalOpsTest<cl_uint>(context, queue, program,
                                             "global_u32_ops");
  ok &= RunIndependentGlobalOpsTest<cl_long>(context, queue, program,
                                             "global_i64_ops");
  ok &= RunIndependentGlobalOpsTest<cl_ulong>(context, queue, program,
                                              "global_u64_ops");
  ok &= RunIndependentLocalOpsTest<cl_int>(context, queue, program,
                                           "local_i32_ops");
  ok &= RunIndependentLocalOpsTest<cl_uint>(context, queue, program,
                                            "local_u32_ops");
  ok &= RunIndependentLocalOpsTest<cl_long>(context, queue, program,
                                            "local_i64_ops");
  ok &= RunIndependentLocalOpsTest<cl_ulong>(context, queue, program,
                                             "local_u64_ops");

  ok &= RunContendedGlobalAddTest<cl_uint>(context, queue, program,
                                           "global_u32_add_contended");
  ok &= RunContendedGlobalAddTest<cl_ulong>(context, queue, program,
                                            "global_u64_add_contended");
  ok &= RunContendedLocalAddTest<cl_uint>(context, queue, program,
                                          "local_u32_add_contended");
  ok &= RunContendedLocalAddTest<cl_ulong>(context, queue, program,
                                           "local_u64_add_contended");

  ok &= RunC11Test(context, queue, device, "-cl-std=CL3.0");

  if (!ok) {
    std::cerr << kRed << "Atomic ops test failed" << kReset << std::endl;
    return -1;
  }

  std::cout << kGreen << "Atomic ops test passed" << kReset << std::endl;
  return 0;
}
