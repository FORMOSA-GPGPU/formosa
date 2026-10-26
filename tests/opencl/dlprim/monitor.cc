// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <ATen/ATen.h>
#include <ATen/record_function.h>
#include <CL/cl.h>
#include <dlfcn.h>

#include <atomic>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace {
std::atomic<bool> transfers_allowed{true};
std::atomic<uint64_t> launches{}, transfers{}, cpu_ops{};

template <typename Function>
Function original(const char *name) {
  auto function = reinterpret_cast<Function>(dlsym(RTLD_NEXT, name));
  TORCH_CHECK(function, "Cannot resolve OpenCL function: ", name);
  return function;
}

void transfer() {
  ++transfers;
  TORCH_CHECK(transfers_allowed.load(), "CPU transfer forbidden");
}

bool cpu_tensor(const c10::IValue &value, bool scalar_input) {
  if (value.isTensor()) {
    const auto &tensor = value.toTensor();
    // ATen wraps literals such as scores / 2.0 in CPU scalar tensors.
    // Inputs of that form are kernel arguments, not CPU tensor computation.
    return tensor.defined() && tensor.device().is_cpu() &&
           !(scalar_input && tensor.dim() == 0);
  }
  if (value.isTensorList()) {
    for (const auto &tensor : value.toTensorVector())
      if (tensor.defined() && tensor.device().is_cpu() &&
          !(scalar_input && tensor.dim() == 0))
        return true;
  }
  if (value.isTuple())
    for (const auto &item : value.toTupleRef().elements())
      if (cpu_tensor(item, scalar_input)) return true;
  return false;
}

template <typename Values>
void inspect(const at::RecordFunction &record, const Values &values,
             bool scalar_input) {
  if (transfers_allowed.load() ||
      !std::string_view(record.name()).starts_with("aten::"))
    return;
  for (const auto &value : values) {
    if (cpu_tensor(value, scalar_input)) {
      ++cpu_ops;
      return;
    }
  }
}
}  // namespace

extern "C" CL_API_ENTRY cl_int CL_API_CALL clEnqueueNDRangeKernel(
    cl_command_queue queue, cl_kernel kernel, cl_uint dimensions,
    const size_t *offset, const size_t *global, const size_t *local,
    cl_uint count, const cl_event *wait, cl_event *event) {
  cl_device_id device{};
  char name[128]{};
  TORCH_CHECK(
      clGetCommandQueueInfo(queue, CL_QUEUE_DEVICE, sizeof(device), &device,
                            nullptr) == CL_SUCCESS &&
          clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(name), name,
                          nullptr) == CL_SUCCESS &&
          std::string_view(name).find("Formosa") != std::string_view::npos,
      "Expected Formosa OpenCL device, got ", name);
  static auto call =
      original<decltype(&clEnqueueNDRangeKernel)>("clEnqueueNDRangeKernel");
  auto result = call(queue, kernel, dimensions, offset, global, local, count,
                     wait, event);
  if (result == CL_SUCCESS) ++launches;
  return result;
}

extern "C" CL_API_ENTRY cl_int CL_API_CALL
clEnqueueReadBuffer(cl_command_queue queue, cl_mem buffer, cl_bool blocking,
                    size_t offset, size_t size, void *pointer, cl_uint count,
                    const cl_event *wait, cl_event *event) {
  transfer();
  static auto call =
      original<decltype(&clEnqueueReadBuffer)>("clEnqueueReadBuffer");
  return call(queue, buffer, blocking, offset, size, pointer, count, wait,
              event);
}

extern "C" CL_API_ENTRY cl_int CL_API_CALL
clEnqueueWriteBuffer(cl_command_queue queue, cl_mem buffer, cl_bool blocking,
                     size_t offset, size_t size, const void *pointer,
                     cl_uint count, const cl_event *wait, cl_event *event) {
  transfer();
  static auto call =
      original<decltype(&clEnqueueWriteBuffer)>("clEnqueueWriteBuffer");
  return call(queue, buffer, blocking, offset, size, pointer, count, wait,
              event);
}

extern "C" CL_API_ENTRY void *CL_API_CALL clEnqueueMapBuffer(
    cl_command_queue queue, cl_mem buffer, cl_bool blocking, cl_map_flags flags,
    size_t offset, size_t size, cl_uint count, const cl_event *wait,
    cl_event *event, cl_int *error) {
  transfer();
  static auto call =
      original<decltype(&clEnqueueMapBuffer)>("clEnqueueMapBuffer");
  return call(queue, buffer, blocking, flags, offset, size, count, wait, event,
              error);
}

namespace {
void initialize_monitor() {
  at::addGlobalCallback(
      at::RecordFunctionCallback(
          [](const at::RecordFunction &record)
              -> std::unique_ptr<at::ObserverContext> {
            inspect(record, record.inputs(), true);
            return nullptr;
          },
          [](const at::RecordFunction &record, at::ObserverContext *) {
            inspect(record, record.outputs(), false);
          })
          .needsInputs(true)
          .needsOutputs(true));

  // Check both detection paths before validating device computation.
  auto x = at::ones({2}, at::TensorOptions().device(
                             c10::Device(c10::DeviceType::PrivateUse1, 0)));
  auto cpu = at::ones({2});
  bool rejected = false;
  transfers_allowed.store(false);
  try {
    at::special_airy_ai(x);
  } catch (const c10::Error &error) {
    rejected = std::string_view(error.what()).find("CPU transfer forbidden") !=
               std::string_view::npos;
  }
  transfers_allowed.store(true);
  TORCH_CHECK(rejected, "Monitor failed to reject CPU fallback");
  const auto before = cpu_ops.load();
  transfers_allowed.store(false);
  cpu.sin();
  transfers_allowed.store(true);
  TORCH_CHECK(cpu_ops.load() > before, "Monitor missed CPU computation");
}
}  // namespace

// Test-only entry points used by the Python smoke test.
extern "C" int dlprim_monitor_initialize() {
  try {
    initialize_monitor();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << std::endl;
    return 1;
  }
}
extern "C" void dlprim_monitor_transfers(bool allow) {
  transfers_allowed.store(allow);
}
extern "C" void dlprim_monitor_counters(uint64_t *values) {
  values[0] = launches.load();
  values[1] = transfers.load();
  values[2] = cpu_ops.load();
}
