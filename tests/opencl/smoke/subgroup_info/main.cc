// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <CL/opencl.hpp>
#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

static void check(bool valid, const char *message) {
  if (!valid) throw std::runtime_error(message);
}

int main() {
  try {
    std::vector<cl::Platform> platforms;
    cl::Platform::get(&platforms);
    check(!platforms.empty(), "No OpenCL platform");
    std::vector<cl::Device> devices;
    platforms.front().getDevices(CL_DEVICE_TYPE_GPU, &devices);
    check(!devices.empty(), "No OpenCL GPU");
    const auto &device = devices.front();
    cl_uint device_max = 0;
    check(
        clGetDeviceInfo(device(), CL_DEVICE_MAX_NUM_SUB_GROUPS,
                        sizeof(device_max), &device_max, nullptr) == CL_SUCCESS,
        "Failed to query the device subgroup limit");
    size_t preferred = 0;
    check(
        clGetDeviceInfo(device(), CL_DEVICE_PREFERRED_WORK_GROUP_SIZE_MULTIPLE,
                        sizeof(preferred), &preferred, nullptr) == CL_SUCCESS,
        "Failed to query the preferred work-group size multiple");
    const size_t width = preferred;
    const size_t max_groups = device_max;
    check(width > 0 && max_groups > 0, "Invalid device subgroup geometry");
    cl::Context context(device);
    cl::CommandQueue queue(context, device);
    cl::Program program(context, R"(
      #pragma OPENCL EXTENSION cl_khr_subgroups : enable
      __kernel void subgroup_info(__global uint *output) {
        size_t n = get_local_size(0) * get_local_size(1) * get_local_size(2);
        size_t lid = get_local_id(0) + get_local_size(0) *
            (get_local_id(1) + get_local_size(1) * get_local_id(2));
        size_t i = (get_group_id(0) * n + lid) * 6;
        output[i] = get_sub_group_id();
        output[i + 1] = get_sub_group_local_id();
        output[i + 2] = get_sub_group_size();
        output[i + 3] = get_max_sub_group_size();
        output[i + 4] = get_num_sub_groups();
        output[i + 5] = get_enqueued_num_sub_groups();
      }
    )");
    try {
      program.build();
    } catch (const cl::Error &) {
      std::cerr << program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(device) << '\n';
      throw;
    }
    cl::Kernel kernel(program, "subgroup_info");
    auto query = [&](cl_kernel_sub_group_info name, size_t input_bytes,
                     const void *input, size_t output_bytes, void *output,
                     size_t *returned = nullptr) {
      return clGetKernelSubGroupInfo(kernel(), device(), name, input_bytes,
                                     input, output_bytes, output, returned);
    };
    size_t value = 0;
    check(query(CL_KERNEL_MAX_NUM_SUB_GROUPS, 0, nullptr, sizeof(value),
                &value) == CL_SUCCESS &&
              value == max_groups,
          "Kernel subgroup limit mismatch");
    check(query(CL_KERNEL_COMPILE_NUM_SUB_GROUPS, 0, nullptr, sizeof(value),
                &value) == CL_SUCCESS &&
              value == 0,
          "Unexpected compile-time subgroup count");
    check(kernel.getWorkGroupInfo<CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE>(
              device) == width,
          "Kernel preferred multiple mismatch");

    for (const std::vector<size_t> local : {std::vector<size_t>{1},
                                            {std::max(size_t{1}, width - 1)},
                                            {width},
                                            {width + 1},
                                            {width * max_groups},
                                            {width, 2},
                                            {2, 2, 2}}) {
      size_t n = 1;
      for (size_t dim : local) n *= dim;
      if (n > width * max_groups) continue;
      const size_t groups = n / width + (n % width != 0);
      const size_t input_bytes = local.size() * sizeof(size_t);
      check(query(CL_KERNEL_MAX_SUB_GROUP_SIZE_FOR_NDRANGE, input_bytes,
                  local.data(), sizeof(value), &value) == CL_SUCCESS &&
                value == width,
            "NDRange subgroup width mismatch");
      check(query(CL_KERNEL_SUB_GROUP_COUNT_FOR_NDRANGE, input_bytes,
                  local.data(), sizeof(value), &value) == CL_SUCCESS &&
                value == groups,
            "NDRange subgroup count mismatch");
      size_t returned = 0;
      check(query(CL_KERNEL_SUB_GROUP_COUNT_FOR_NDRANGE, input_bytes,
                  local.data(), 0, nullptr, &returned) == CL_SUCCESS &&
                returned == sizeof(size_t),
            "Query result size mismatch");
      std::array<size_t, 3> shape{1, 1, 1};
      std::copy(local.begin(), local.end(), shape.begin());
      std::vector<cl_uint> output(n * 3 * 6, ~cl_uint{0});
      cl::Buffer buffer(context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                        output.size() * sizeof(cl_uint), output.data());
      kernel.setArg(0, buffer);
      queue.enqueueNDRangeKernel(kernel, cl::NullRange,
                                 cl::NDRange(shape[0] * 3, shape[1], shape[2]),
                                 cl::NDRange(shape[0], shape[1], shape[2]));
      queue.enqueueReadBuffer(buffer, CL_TRUE, 0,
                              output.size() * sizeof(cl_uint), output.data());
      for (size_t i = 0; i < n * 3; ++i) {
        const size_t lid = i % n;
        const size_t expected[] = {lid / width,
                                   lid % width,
                                   std::min(width, n - (lid / width) * width),
                                   width,
                                   groups,
                                   groups};
        for (size_t field = 0; field < 6; ++field)
          check(output[i * 6 + field] == expected[field],
                "Kernel subgroup builtin differs from hardware geometry");
      }
      std::cout << "PASS: local=" << shape[0] << 'x' << shape[1] << 'x'
                << shape[2] << ", subgroup size=" << width
                << ", count=" << groups << '\n';
    }

    for (size_t count : {size_t{0}, size_t{1}, max_groups, max_groups + 1}) {
      for (size_t dims = 1; dims <= 3; ++dims) {
        size_t local[3] = {99, 99, 99}, returned = 0;
        check(query(CL_KERNEL_LOCAL_SIZE_FOR_SUB_GROUP_COUNT, sizeof(count),
                    &count, dims * sizeof(size_t), local,
                    &returned) == CL_SUCCESS &&
                  returned == dims * sizeof(size_t),
              "Local size query failed");
        for (size_t i = 0; i < dims; ++i)
          check(local[i] == (count == 0 || count > max_groups ? 0
                             : i == 0                         ? count * width
                                                              : 1),
                "Local size mismatch");
      }
    }
    const size_t local[4] = {width, 1, 1, 1};
    for (auto name : {CL_KERNEL_MAX_SUB_GROUP_SIZE_FOR_NDRANGE,
                      CL_KERNEL_SUB_GROUP_COUNT_FOR_NDRANGE}) {
      for (size_t bytes : {size_t{0}, sizeof(size_t) - 1, sizeof(local)})
        check(query(name, bytes, local, sizeof(value), &value) ==
                  CL_INVALID_VALUE,
              "Invalid NDRange input size accepted");
      check(query(name, sizeof(size_t), nullptr, sizeof(value), &value) ==
                CL_INVALID_VALUE,
            "Null NDRange input accepted");
      for (size_t bad : {size_t{0}, std::numeric_limits<size_t>::max()})
        check(query(name, sizeof(bad), &bad, sizeof(value), &value) ==
                  CL_INVALID_VALUE,
              "Invalid NDRange extent accepted");
      check(query(name, sizeof(size_t), local, sizeof(value) - 1, &value) ==
                CL_INVALID_VALUE,
            "Undersized result accepted");
    }
    check(query(CL_KERNEL_LOCAL_SIZE_FOR_SUB_GROUP_COUNT, 0, local,
                sizeof(value), &value) == CL_INVALID_VALUE,
          "Invalid count size accepted");
    check(query(CL_KERNEL_LOCAL_SIZE_FOR_SUB_GROUP_COUNT, sizeof(size_t),
                nullptr, sizeof(value), &value) == CL_INVALID_VALUE,
          "Null count accepted");
    check(query(CL_KERNEL_LOCAL_SIZE_FOR_SUB_GROUP_COUNT, sizeof(size_t), local,
                sizeof(value) - 1, &value) == CL_INVALID_VALUE,
          "Short local size output accepted");
    std::cout << "PASS: subgroup queries and builtins\n";
  } catch (const cl::Error &error) {
    std::cerr << error.what() << ": " << error.err() << '\n';
    return 1;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
