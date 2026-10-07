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
#include <vector>

int main() {
  constexpr int width = 8;
  constexpr int height = 8;
  constexpr int channels = 4;
  std::vector<uint8_t> input(width * height * channels);
  std::vector<uint8_t> output(input.size());
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x)
      for (int c = 0; c < channels; ++c)
        input[(y * width + x) * channels + c] =
            (x * 17 + y * 11 + c * 23) % 256;

  try {
    std::vector<cl::Platform> platforms;
    cl::Platform::get(&platforms);
    std::vector<cl::Device> devices;
    for (const auto &platform : platforms)
      platform.getDevices(CL_DEVICE_TYPE_GPU, &devices);
    if (devices.empty()) {
      std::cerr << "No OpenCL GPU device\n";
      return 1;
    }

    cl::Context context({devices.front()});
    cl::CommandQueue queue(context, devices.front());
    std::ifstream kernel_file(KERNEL_PATH);
    if (!kernel_file) {
      std::cerr << "Cannot open " << KERNEL_PATH << '\n';
      return 1;
    }
    std::string source(std::istreambuf_iterator<char>{kernel_file}, {});
    cl::Program program(context, source);
    program.build({devices.front()});

    cl::ImageFormat format(CL_RGBA, CL_UNSIGNED_INT8);
    cl::Image2D src(context, CL_MEM_READ_ONLY, format, width, height);
    cl::Image2D dst(context, CL_MEM_WRITE_ONLY, format, width, height);
    cl::Sampler sampler(context, CL_FALSE, CL_ADDRESS_CLAMP_TO_EDGE,
                        CL_FILTER_NEAREST);
    const std::array<size_t, 2> origin{0, 0};
    const std::array<size_t, 2> region{width, height};
    queue.enqueueWriteImage(src, CL_TRUE, origin, region, 0, 0, input.data());
    cl::Kernel kernel(program, "image_blur");
    kernel.setArg(0, src);
    kernel.setArg(1, dst);
    kernel.setArg(2, sampler);
    queue.enqueueNDRangeKernel(kernel, cl::NullRange,
                               cl::NDRange(width, height), cl::NDRange(2, 2));
    queue.enqueueReadImage(dst, CL_TRUE, origin, region, 0, 0, output.data());
  } catch (const cl::Error &error) {
    std::cerr << error.what() << ": " << error.err() << '\n';
    return 1;
  }

  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x)
      for (int c = 0; c < channels; ++c) {
        int sum = 0;
        for (int dy = -1; dy <= 1; ++dy)
          for (int dx = -1; dx <= 1; ++dx) {
            int sx = std::clamp(x + dx, 0, width - 1);
            int sy = std::clamp(y + dy, 0, height - 1);
            sum += input[(sy * width + sx) * channels + c];
          }
        const size_t index = (y * width + x) * channels + c;
        if (output[index] != sum / 9) {
          std::cerr << "Mismatch at (" << x << ", " << y << ") channel " << c
                    << ": got " << static_cast<int>(output[index])
                    << ", expected " << sum / 9 << '\n';
          return 1;
        }
      }
  std::cout << "8x8 RGBA image blur passed\n";
  return 0;
}
