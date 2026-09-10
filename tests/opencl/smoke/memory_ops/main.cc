// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <CL/opencl.hpp>
#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr std::array<size_t, 8> kPatternSizes = {1, 2, 4, 8, 16, 32, 64, 128};
constexpr size_t kFillSize = 256;
constexpr uint8_t kSentinel = 0xa5;

constexpr size_t kMapBufferSize = 256;
constexpr size_t kMapOffset = 7;
constexpr size_t kMapSize = 73;

bool check_bytes(const std::vector<uint8_t> &actual,
                 const std::vector<uint8_t> &expected,
                 const std::string &name) {
  for (size_t i = 0; i < expected.size(); ++i) {
    if (actual[i] != expected[i]) {
      std::cerr << name << " mismatch at byte " << i << ": got "
                << static_cast<unsigned>(actual[i]) << ", expected "
                << static_cast<unsigned>(expected[i]) << '\n';
      return false;
    }
  }
  return true;
}

bool unmap_and_check(cl::CommandQueue &queue, cl::Buffer &buffer, void *mapped,
                     const std::vector<uint8_t> &expected,
                     const std::string &name) {
  cl::Event unmap_event;
  queue.enqueueUnmapMemObject(buffer, mapped, nullptr, &unmap_event);
  unmap_event.wait();

  std::vector<uint8_t> actual(expected.size());
  queue.enqueueReadBuffer(buffer, CL_TRUE, 0, actual.size(), actual.data());
  return check_bytes(actual, expected, name);
}

bool test_fill(cl::CommandQueue &queue, const cl::Context &context,
               const cl::Device &device) {
  const size_t alignment_bits = device.getInfo<CL_DEVICE_MEM_BASE_ADDR_ALIGN>();
  const size_t alignment_bytes =
      std::max<size_t>(1, (alignment_bits + CHAR_BIT - 1) / CHAR_BIT);
  const size_t sub_origin = alignment_bytes;
  constexpr size_t kSubBufferSize = 512;
  const size_t parent_size = sub_origin + kSubBufferSize + 128;

  cl::Buffer parent(context, CL_MEM_READ_WRITE, parent_size);
  std::vector<uint8_t> initial(parent_size, kSentinel);
  queue.enqueueWriteBuffer(parent, CL_TRUE, 0, initial.size(), initial.data());
  const cl_buffer_region region{sub_origin, kSubBufferSize};
  cl::Buffer sub_buffer = parent.createSubBuffer(
      CL_MEM_READ_WRITE, CL_BUFFER_CREATE_TYPE_REGION, &region);

  for (size_t pattern_size : kPatternSizes) {
    cl_int err =
        clEnqueueFillBuffer(queue(), parent(), &kSentinel, sizeof(kSentinel), 0,
                            parent_size, 0, nullptr, nullptr);
    if (err != CL_SUCCESS) {
      std::cerr << "clEnqueueFillBuffer(sentinel) failed with " << err << '\n';
      return false;
    }

    std::vector<uint8_t> pattern(pattern_size);
    for (size_t i = 0; i < pattern.size(); ++i)
      pattern[i] =
          static_cast<uint8_t>(0x10 + ((i * 37 + pattern_size) & 0x7f));

    const size_t offset = pattern_size;
    err = clEnqueueFillBuffer(queue(), sub_buffer(), pattern.data(),
                              pattern.size(), offset, kFillSize, 0, nullptr,
                              nullptr);
    if (err != CL_SUCCESS) {
      std::cerr << "clEnqueueFillBuffer(pattern size " << pattern_size
                << ") failed with " << err << '\n';
      return false;
    }

    std::vector<uint8_t> actual(parent_size);
    queue.enqueueReadBuffer(parent, CL_TRUE, 0, actual.size(), actual.data());

    std::vector<uint8_t> expected(parent_size, kSentinel);
    for (size_t i = 0; i < kFillSize; ++i)
      expected[sub_origin + offset + i] = pattern[i % pattern.size()];

    if (!check_bytes(actual, expected,
                     "fill pattern size " + std::to_string(pattern_size)))
      return false;

    std::cout << "fill pattern size " << pattern_size << " passed\n";
  }

  return true;
}

bool test_maps(cl::CommandQueue &queue, const cl::Context &context) {
  cl::Buffer buffer(context, CL_MEM_READ_WRITE, kMapBufferSize);

  // CL_MAP_READ: the mapped bytes must reflect the device contents, and the
  // read-only unmap must leave the buffer unchanged.
  std::vector<uint8_t> expected(kMapBufferSize);
  for (size_t i = 0; i < expected.size(); ++i)
    expected[i] = static_cast<uint8_t>((i * 13 + 9) & 0xff);
  queue.enqueueWriteBuffer(buffer, CL_TRUE, 0, expected.size(),
                           expected.data());

  cl_int err = CL_SUCCESS;
  cl::Event map_event;
  void *mapped =
      queue.enqueueMapBuffer(buffer, CL_TRUE, CL_MAP_READ, kMapOffset, kMapSize,
                             nullptr, &map_event, &err);
  if (err != CL_SUCCESS || mapped == nullptr) {
    std::cerr << "clEnqueueMapBuffer(READ) failed with " << err << '\n';
    return false;
  }
  map_event.wait();
  const bool read_ok = std::equal(expected.begin() + kMapOffset,
                                  expected.begin() + kMapOffset + kMapSize,
                                  static_cast<const uint8_t *>(mapped));
  if (!unmap_and_check(queue, buffer, mapped, expected, "map READ"))
    return false;
  if (!read_ok) {
    std::cerr << "map READ returned unexpected bytes\n";
    return false;
  }
  std::cout << "map READ passed\n";

  // CL_MAP_WRITE: changes become visible only after unmap.
  expected.assign(kMapBufferSize, kSentinel);
  queue.enqueueWriteBuffer(buffer, CL_TRUE, 0, expected.size(),
                           expected.data());
  mapped = queue.enqueueMapBuffer(buffer, CL_TRUE, CL_MAP_WRITE, kMapOffset,
                                  kMapSize, nullptr, &map_event, &err);
  if (err != CL_SUCCESS || mapped == nullptr) {
    std::cerr << "clEnqueueMapBuffer(WRITE) failed with " << err << '\n';
    return false;
  }
  map_event.wait();
  for (size_t i = 0; i < kMapSize; ++i)
    expected[kMapOffset + i] = static_cast<uint8_t>(0x30 + (i * 5 & 0x3f));
  std::copy(expected.begin() + kMapOffset,
            expected.begin() + kMapOffset + kMapSize,
            static_cast<uint8_t *>(mapped));
  if (!unmap_and_check(queue, buffer, mapped, expected, "map WRITE"))
    return false;
  std::cout << "map WRITE passed\n";

  // CL_MAP_READ | CL_MAP_WRITE: verify the initial contents before modifying
  // the mapped region.
  expected.resize(kMapBufferSize);
  for (size_t i = 0; i < expected.size(); ++i)
    expected[i] = static_cast<uint8_t>((i * 7 + 0x42) & 0xff);
  queue.enqueueWriteBuffer(buffer, CL_TRUE, 0, expected.size(),
                           expected.data());
  mapped =
      queue.enqueueMapBuffer(buffer, CL_TRUE, CL_MAP_READ | CL_MAP_WRITE,
                             kMapOffset, kMapSize, nullptr, &map_event, &err);
  if (err != CL_SUCCESS || mapped == nullptr) {
    std::cerr << "clEnqueueMapBuffer(READ|WRITE) failed with " << err << '\n';
    return false;
  }
  map_event.wait();
  auto *mapped_bytes = static_cast<uint8_t *>(mapped);
  const bool read_write_read_ok =
      std::equal(expected.begin() + kMapOffset,
                 expected.begin() + kMapOffset + kMapSize, mapped_bytes);
  for (size_t i = 0; i < kMapSize; ++i) {
    expected[kMapOffset + i] = static_cast<uint8_t>(mapped_bytes[i] ^ 0x5a);
    mapped_bytes[i] = expected[kMapOffset + i];
  }
  if (!unmap_and_check(queue, buffer, mapped, expected, "map READ|WRITE"))
    return false;
  if (!read_write_read_ok) {
    std::cerr << "map READ|WRITE returned unexpected bytes\n";
    return false;
  }
  std::cout << "map READ|WRITE passed\n";

  // CL_MAP_WRITE_INVALIDATE_REGION does not need to preserve the old device
  // contents; verify the new contents after unmap instead.
  expected.assign(kMapBufferSize, 0x6b);
  queue.enqueueWriteBuffer(buffer, CL_TRUE, 0, expected.size(),
                           expected.data());
  mapped =
      queue.enqueueMapBuffer(buffer, CL_TRUE, CL_MAP_WRITE_INVALIDATE_REGION,
                             kMapOffset, kMapSize, nullptr, &map_event, &err);
  if (err != CL_SUCCESS || mapped == nullptr) {
    std::cerr << "clEnqueueMapBuffer(WRITE_INVALIDATE_REGION) failed with "
              << err << '\n';
    return false;
  }
  map_event.wait();
  for (size_t i = 0; i < kMapSize; ++i)
    expected[kMapOffset + i] = static_cast<uint8_t>(0xc0 + (i & 0x1f));
  std::copy(expected.begin() + kMapOffset,
            expected.begin() + kMapOffset + kMapSize,
            static_cast<uint8_t *>(mapped));
  if (!unmap_and_check(queue, buffer, mapped, expected,
                       "map WRITE_INVALIDATE_REGION"))
    return false;
  std::cout << "map WRITE_INVALIDATE_REGION passed\n";

  return true;
}

}  // namespace

int main() {
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

    const cl::Device &device = devices.front();
    std::cout << "Device: " << device.getInfo<CL_DEVICE_NAME>() << '\n';
    cl::Context context(device);
    cl::CommandQueue queue(context, device);

    if (!test_fill(queue, context, device) || !test_maps(queue, context))
      return 1;
  } catch (const cl::Error &error) {
    std::cerr << error.what() << " failed with " << error.err() << '\n';
    return 1;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }

  std::cout << "memory_ops smoke test passed\n";
  return 0;
}
