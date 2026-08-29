// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <CL/opencl.h>

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "integer_mad_spirv64.h"

namespace {

constexpr std::size_t kElements = 16;
constexpr std::size_t kLocalSize = 8;

std::string getDeviceString(cl_device_id device, cl_device_info param) {
  size_t size = 0;
  if (clGetDeviceInfo(device, param, 0, nullptr, &size) != CL_SUCCESS ||
      size == 0) {
    return {};
  }

  std::string value(size, '\0');
  if (clGetDeviceInfo(device, param, size, value.data(), nullptr) !=
      CL_SUCCESS) {
    return {};
  }

  if (!value.empty() && value.back() == '\0') {
    value.pop_back();
  }
  return value;
}

bool findGpuDevice(cl_platform_id &platform, cl_device_id &device) {
  cl_uint num_platforms = 0;
  if (clGetPlatformIDs(0, nullptr, &num_platforms) != CL_SUCCESS ||
      num_platforms == 0) {
    return false;
  }

  std::vector<cl_platform_id> platforms(num_platforms);
  if (clGetPlatformIDs(num_platforms, platforms.data(), nullptr) !=
      CL_SUCCESS) {
    return false;
  }

  for (cl_platform_id candidate_platform : platforms) {
    cl_uint num_devices = 0;
    cl_int err = clGetDeviceIDs(candidate_platform, CL_DEVICE_TYPE_GPU, 0,
                                nullptr, &num_devices);
    if (err != CL_SUCCESS || num_devices == 0) {
      continue;
    }

    std::vector<cl_device_id> devices(num_devices);
    err = clGetDeviceIDs(candidate_platform, CL_DEVICE_TYPE_GPU, num_devices,
                         devices.data(), nullptr);
    if (err != CL_SUCCESS || devices.empty()) {
      continue;
    }

    platform = candidate_platform;
    device = devices.front();
    return true;
  }

  return false;
}

cl_program createProgramWithIL(cl_platform_id platform, cl_context context,
                               const void *il, std::size_t il_size,
                               cl_int *errcode_ret) {
  auto create_program_with_il_khr =
      reinterpret_cast<clCreateProgramWithILKHR_fn>(
          clGetExtensionFunctionAddressForPlatform(platform,
                                                   "clCreateProgramWithILKHR"));
  if (create_program_with_il_khr == nullptr) {
    if (errcode_ret != nullptr) {
      *errcode_ret = CL_INVALID_OPERATION;
    }
    return nullptr;
  }

  return create_program_with_il_khr(context, il, il_size, errcode_ret);
}

void dumpBuildLog(cl_program program, cl_device_id device) {
  size_t size = 0;
  if (clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, nullptr,
                            &size) != CL_SUCCESS ||
      size == 0) {
    return;
  }

  std::string log(size, '\0');
  if (clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, size,
                            log.data(), nullptr) == CL_SUCCESS) {
    std::cerr << "Build log:\n" << log << std::endl;
  }
}

}  // namespace

int main() {
  cl_platform_id platform = nullptr;
  cl_device_id device = nullptr;
  cl_context context = nullptr;
  cl_command_queue queue = nullptr;
  cl_program program = nullptr;
  cl_kernel kernel = nullptr;
  cl_mem buf_a = nullptr;
  cl_mem buf_b = nullptr;
  cl_mem buf_c = nullptr;
  cl_int err = CL_SUCCESS;
  int rc = 1;
  std::vector<cl_uint> src_a(kElements);
  std::vector<cl_uint> src_b(kElements);
  std::vector<cl_uint> dst(kElements, 0);

  if (!findGpuDevice(platform, device)) {
    std::cerr << "No OpenCL GPU device found" << std::endl;
    goto cleanup;
  }

  std::cout << "Device: " << getDeviceString(device, CL_DEVICE_NAME)
            << std::endl;

  {
    const std::string il_version =
        getDeviceString(device, CL_DEVICE_IL_VERSION_KHR);
    std::cout << "CL_DEVICE_IL_VERSION_KHR: " << il_version << std::endl;
    if (il_version.find("SPIR-V_") == std::string::npos) {
      std::cerr << "Device does not report SPIR-V IL support" << std::endl;
      goto cleanup;
    }
  }

  context = clCreateContext(nullptr, 1, &device, nullptr, nullptr, &err);
  if (err != CL_SUCCESS || context == nullptr) {
    std::cerr << "clCreateContext failed: " << err << std::endl;
    goto cleanup;
  }

  queue = clCreateCommandQueue(context, device, 0, &err);
  if (err != CL_SUCCESS || queue == nullptr) {
    std::cerr << "clCreateCommandQueue failed: " << err << std::endl;
    goto cleanup;
  }

  program = createProgramWithIL(platform, context, kExample0Spirv64,
                                kExample0Spirv64Len, &err);
  if (err != CL_SUCCESS || program == nullptr) {
    std::cerr << "clCreateProgramWithIL failed: " << err << std::endl;
    goto cleanup;
  }

  err = clBuildProgram(program, 1, &device, nullptr, nullptr, nullptr);
  if (err != CL_SUCCESS) {
    std::cerr << "clBuildProgram failed: " << err << std::endl;
    dumpBuildLog(program, device);
    goto cleanup;
  }

  kernel = clCreateKernel(program, "integer_mad", &err);
  if (err != CL_SUCCESS || kernel == nullptr) {
    std::cerr << "clCreateKernel failed: " << err << std::endl;
    goto cleanup;
  }

  for (std::size_t i = 0; i < kElements; ++i) {
    src_a[i] = static_cast<cl_uint>(i + 1);
    src_b[i] = static_cast<cl_uint>((i * 3) + 5);
  }

  buf_a = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                         sizeof(cl_uint) * src_a.size(), src_a.data(), &err);
  if (err != CL_SUCCESS || buf_a == nullptr) {
    std::cerr << "clCreateBuffer(a) failed: " << err << std::endl;
    goto cleanup;
  }

  buf_b = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                         sizeof(cl_uint) * src_b.size(), src_b.data(), &err);
  if (err != CL_SUCCESS || buf_b == nullptr) {
    std::cerr << "clCreateBuffer(b) failed: " << err << std::endl;
    goto cleanup;
  }

  buf_c = clCreateBuffer(context, CL_MEM_WRITE_ONLY,
                         sizeof(cl_uint) * dst.size(), nullptr, &err);
  if (err != CL_SUCCESS || buf_c == nullptr) {
    std::cerr << "clCreateBuffer(c) failed: " << err << std::endl;
    goto cleanup;
  }

  err = clSetKernelArg(kernel, 0, sizeof(cl_mem), &buf_a);
  err |= clSetKernelArg(kernel, 1, sizeof(cl_mem), &buf_b);
  err |= clSetKernelArg(kernel, 2, sizeof(cl_mem), &buf_c);
  if (err != CL_SUCCESS) {
    std::cerr << "clSetKernelArg failed: " << err << std::endl;
    goto cleanup;
  }

  {
    const size_t global_work_size[1] = {kElements};
    const size_t local_work_size[1] = {kLocalSize};
    err = clEnqueueNDRangeKernel(queue, kernel, 1, nullptr, global_work_size,
                                 local_work_size, 0, nullptr, nullptr);
    if (err != CL_SUCCESS) {
      std::cerr << "clEnqueueNDRangeKernel failed: " << err << std::endl;
      goto cleanup;
    }
  }

  err = clEnqueueReadBuffer(queue, buf_c, CL_TRUE, 0,
                            sizeof(cl_uint) * dst.size(), dst.data(), 0,
                            nullptr, nullptr);
  if (err != CL_SUCCESS) {
    std::cerr << "clEnqueueReadBuffer failed: " << err << std::endl;
    goto cleanup;
  }

  for (std::size_t i = 0; i < kElements; ++i) {
    const cl_uint expected = src_a[i] * 7 + src_b[i];
    if (dst[i] != expected) {
      std::cerr << "Mismatch at " << i << ": got " << dst[i] << ", expected "
                << expected << std::endl;
      goto cleanup;
    }
  }

  std::cout << "SPIR-V smoke test passed" << std::endl;
  rc = 0;

cleanup:
  if (buf_c != nullptr) {
    clReleaseMemObject(buf_c);
  }
  if (buf_b != nullptr) {
    clReleaseMemObject(buf_b);
  }
  if (buf_a != nullptr) {
    clReleaseMemObject(buf_a);
  }
  if (kernel != nullptr) {
    clReleaseKernel(kernel);
  }
  if (program != nullptr) {
    clReleaseProgram(program);
  }
  if (queue != nullptr) {
    clReleaseCommandQueue(queue);
  }
  if (context != nullptr) {
    clReleaseContext(context);
  }
  return rc;
}
