// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <CL/opencl.hpp>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
  constexpr size_t kBufferSize = 320;
  constexpr size_t kSourceOffset = 3;
  constexpr size_t kDestinationOffset = 5;
  constexpr size_t kCopySize = 257;
  constexpr uint8_t kSentinel = 0xa5;

  try {
    std::vector<cl::Platform> platforms;
    cl::Platform::get(&platforms);
    if (platforms.empty()) {
      std::cerr << "No OpenCL platform found\n";
      return 1;
    }

    std::vector<cl::Device> devices;
    platforms.front().getDevices(CL_DEVICE_TYPE_GPU, &devices);
    if (devices.empty()) {
      std::cerr << "No OpenCL GPU device found\n";
      return 1;
    }

    cl::Context context(devices.front());
    cl::CommandQueue queue(context, devices.front());
    cl::Buffer source(context, CL_MEM_READ_WRITE, kBufferSize);
    cl::Buffer destination(context, CL_MEM_READ_WRITE, kBufferSize);

    std::vector<uint8_t> source_data(kBufferSize);
    std::vector<uint8_t> destination_data(kBufferSize, kSentinel);
    for (size_t i = 0; i < source_data.size(); ++i) {
      source_data[i] = static_cast<uint8_t>((i * 17 + 11) & 0xff);
    }

    // Keep the host vectors alive until their events complete. The writes,
    // D2D copy, and read are all submitted non-blocking so this exercises the
    // unified command stream and its completion adapter.
    cl::Event source_write;
    cl::Event destination_write;
    queue.enqueueWriteBuffer(source, CL_FALSE, 0, source_data.size(),
                             source_data.data(), nullptr, &source_write);
    queue.enqueueWriteBuffer(destination, CL_FALSE, 0, destination_data.size(),
                             destination_data.data(), nullptr,
                             &destination_write);

    cl::Event copy_event;
    queue.enqueueCopyBuffer(source, destination, kSourceOffset,
                            kDestinationOffset, kCopySize, nullptr,
                            &copy_event);

    std::vector<uint8_t> result(kBufferSize, 0);
    cl::Event read_event;
    queue.enqueueReadBuffer(destination, CL_FALSE, 0, result.size(),
                            result.data(), nullptr, &read_event);
    queue.flush();
    read_event.wait();

    for (size_t i = 0; i < result.size(); ++i) {
      uint8_t expected = kSentinel;
      if (i >= kDestinationOffset && i < kDestinationOffset + kCopySize) {
        expected = source_data[kSourceOffset + i - kDestinationOffset];
      }
      if (result[i] != expected) {
        std::cerr << "CopyBuffer mismatch at byte " << i << ": got "
                  << static_cast<unsigned>(result[i]) << ", expected "
                  << static_cast<unsigned>(expected) << '\n';
        return 1;
      }
    }

    // Exercise sub-buffer metadata and non-zero origins. Copy offsets are
    // relative to the sub-buffers, while the final read observes the parent.
    constexpr size_t kParentSize = 512;
    // The OpenCL sub-buffer origin must satisfy the device's advertised
    // CL_DEVICE_MEM_BASE_ADDR_ALIGN (128 bytes on Formosa); keep the copy
    // offset itself intentionally
    // unaligned to continue exercising byte-granular address arithmetic.
    constexpr size_t kSubOrigin = 128;
    constexpr size_t kSubSize = 123;
    constexpr size_t kSubDestinationOrigin = 256;
    constexpr size_t kSubCopyOffset = 5;
    constexpr size_t kSubCopySize = 97;
    cl::Buffer parent(context, CL_MEM_READ_WRITE, kParentSize);
    std::vector<uint8_t> parent_data(kParentSize);
    for (size_t i = 0; i < parent_data.size(); ++i)
      parent_data[i] = static_cast<uint8_t>((i * 29 + 3) & 0xff);
    queue.enqueueWriteBuffer(parent, CL_TRUE, 0, parent_data.size(),
                             parent_data.data());
    cl_buffer_region src_region{kSubOrigin, kSubSize};
    cl_buffer_region dst_region{kSubDestinationOrigin, kSubSize};
    cl::Buffer sub_source = parent.createSubBuffer(
        CL_MEM_READ_WRITE, CL_BUFFER_CREATE_TYPE_REGION, &src_region);
    cl::Buffer sub_destination = parent.createSubBuffer(
        CL_MEM_READ_WRITE, CL_BUFFER_CREATE_TYPE_REGION, &dst_region);
    queue.enqueueCopyBuffer(sub_source, sub_destination, kSubCopyOffset,
                            kSubCopyOffset, kSubCopySize);
    queue.enqueueReadBuffer(parent, CL_TRUE, 0, parent_data.size(),
                            parent_data.data());
    for (size_t i = 0; i < kSubCopySize; ++i) {
      const size_t dst = kSubDestinationOrigin + kSubCopyOffset + i;
      const size_t src = kSubOrigin + kSubCopyOffset + i;
      if (parent_data[dst] != parent_data[src]) {
        std::cerr << "sub-buffer CopyBuffer mismatch at byte " << dst << '\n';
        return 1;
      }
    }

    // Submit enough event-backed copies to wrap the command ring. Events stay
    // live until all slots retire, so completion slots must not alias.
    std::vector<cl::Event> outstanding;
    outstanding.reserve(80);
    for (size_t i = 0; i < 80; ++i) {
      cl::Event event;
      queue.enqueueCopyBuffer(source, destination, i % 16, 32 + (i % 16), 1,
                              nullptr, &event);
      outstanding.push_back(event);
    }
    queue.flush();
    for (cl::Event &event : outstanding) event.wait();

    queue.enqueueWriteBuffer(destination, CL_TRUE, 0, destination_data.size(),
                             destination_data.data());
    const size_t rect_source_origin[3] = {3, 1, 0};
    const size_t rect_destination_origin[3] = {5, 1, 0};
    const size_t rect_region[3] = {7, 2, 2};
    constexpr size_t kSourceRowPitch = 16, kSourceSlicePitch = 64;
    constexpr size_t kDestinationRowPitch = 20, kDestinationSlicePitch = 80;
    cl_event rect_event = nullptr;
    const cl_int enqueue_status = clEnqueueCopyBufferRect(
        queue(), source(), destination(), rect_source_origin,
        rect_destination_origin, rect_region, kSourceRowPitch,
        kSourceSlicePitch, kDestinationRowPitch, kDestinationSlicePitch, 0,
        nullptr, &rect_event);
    if (enqueue_status != CL_SUCCESS || rect_event == nullptr) {
      std::cerr << "CopyBufferRect enqueue failed with " << enqueue_status
                << '\n';
      return 1;
    }
    const cl_int wait_status = clWaitForEvents(1, &rect_event);
    clReleaseEvent(rect_event);
    if (wait_status != CL_SUCCESS) {
      std::cerr << "CopyBufferRect execution failed: " << wait_status << '\n';
      return 1;
    }
    queue.enqueueReadBuffer(destination, CL_TRUE, 0, result.size(),
                            result.data());
    auto expected_rect = destination_data;
    for (size_t z = 0; z < rect_region[2]; ++z)
      for (size_t y = 0; y < rect_region[1]; ++y)
        for (size_t x = 0; x < rect_region[0]; ++x) {
          const size_t src = z * kSourceSlicePitch + (y + 1) * kSourceRowPitch +
                             rect_source_origin[0] + x;
          const size_t dst = z * kDestinationSlicePitch +
                             (y + 1) * kDestinationRowPitch +
                             rect_destination_origin[0] + x;
          expected_rect[dst] = source_data[src];
        }
    if (result != expected_rect) {
      std::cerr << "CopyBufferRect data or untouched bytes mismatch\n";
      return 1;
    }
  } catch (const cl::Error &error) {
    std::cerr << error.what() << " failed with " << error.err() << '\n';
    return 1;
  }

  std::cout << "CopyBuffer passed\n";
  return 0;
}
