#include <CL/opencl.hpp>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

#ifndef KERNEL_PATH
#define KERNEL_PATH "./kernel.cl"
#endif

namespace {

bool parse_uint(const char *text, cl_uint &value) {
  const std::string input(text);
  const auto parsed =
      std::from_chars(input.data(), input.data() + input.size(), value);
  return parsed.ec == std::errc() && parsed.ptr == input.data() + input.size();
}

// PCG RXS-M-XS, with explicit modulo-2^32 arithmetic in the CPU reference.
uint32_t pcg_hash(uint32_t input) {
  const uint32_t state =
      static_cast<uint32_t>(uint64_t(input) * 747796405u + 2891336453u);
  const uint32_t word = static_cast<uint32_t>(
      uint64_t((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u);
  return (word >> 22u) ^ word;
}

bool inside_circle(uint32_t sample, uint32_t offset) {
  const uint64_t x = pcg_hash(2u * sample + offset) >> 8u;
  const uint64_t y = pcg_hash(2u * sample + offset + 1u) >> 8u;
  return x * x + y * y < (uint64_t{1} << 48u);
}

}  // namespace

int main(int argc, char *argv[]) {
  cl_uint n_points = 0;
  cl_uint seed = 42;
  // Two distinct 32-bit counters per point, without wrapping the sample index.
  if ((argc != 2 && argc != 3) || !parse_uint(argv[1], n_points) ||
      n_points == 0 || n_points > (cl_uint{1} << 31u) ||
      (argc == 3 && !parse_uint(argv[2], seed)) ||
      n_points > std::numeric_limits<size_t>::max() / sizeof(cl_int)) {
    std::cerr << "Usage: " << argv[0]
              << " <samples: 1..2147483648> [seed: uint32]\n";
    return 1;
  }

  std::cout << "Samples: " << n_points << "\nSeed: " << seed << "\n";

  try {
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
      std::cerr << "No Devices!" << std::endl;
      return -1;
    }

    cl::Device device = devices[0];
    cl::Context context({device});
    cl::CommandQueue queue(context, device);

    std::ifstream t(KERNEL_PATH);
    if (!t) {
      std::cerr << "Error Opening Kernel Source file\n";
      return -1;
    }

    std::string source = {std::istreambuf_iterator<char>(t),
                          std::istreambuf_iterator<char>()};

    cl::Program::Sources sources = {source};

    cl::Program program(context, sources);
    try {
      program.build();
    } catch (const cl::Error &) {
      std::cerr << "OpenCL program build failed:\n"
                << program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(device) << "\n";
      return 1;
    }
    cl::Kernel montecarlo(program, "montecarlo");

    const size_t result_bytes = sizeof(cl_int) * size_t(n_points);
    cl::Buffer result_buf(context, CL_MEM_WRITE_ONLY, result_bytes);
    std::vector<cl_int> result(n_points);

    montecarlo.setArg(0, result_buf);
    montecarlo.setArg(1, n_points);
    montecarlo.setArg(2, seed);

    // Round up to complete work-groups; the kernel skips excess work-items.
    constexpr size_t local_size = 64;
    const size_t global_size =
        ((size_t(n_points) + local_size - 1) / local_size) * local_size;
    queue.enqueueNDRangeKernel(montecarlo, cl::NullRange,
                               cl::NDRange(global_size),
                               cl::NDRange(local_size));
    queue.enqueueReadBuffer(result_buf, CL_TRUE, 0, result_bytes,
                            result.data());

    size_t in_arc = 0;
    const uint32_t offset = pcg_hash(seed);
    for (cl_uint i = 0; i < n_points; ++i) {
      const cl_int expected = inside_circle(i, offset);
      if (result[i] != expected) {
        std::cerr << "Validation: FAIL at sample " << i << ": expected "
                  << expected << ", got " << result[i] << "\n";
        return 1;
      }
      in_arc += result[i];
    }

    const double pi = 4.0 * static_cast<double>(in_arc) / n_points;
    // For Bernoulli hits, stddev(4 * mean) <= 2/sqrt(N). Allow six standard
    // deviations plus the discretization error of 24-bit coordinates.
    const double tolerance =
        12.0 / std::sqrt(double(n_points)) + 4.0 / (1u << 24u);
    std::cout << in_arc << " / " << n_points << "\n"
              << std::setprecision(10) << "Pi = " << pi << "\n"
              << "Statistical tolerance: " << tolerance << "\n";
    if (std::abs(pi - std::acos(-1.0)) > tolerance) {
      std::cerr
          << "Validation: FAIL (estimate outside statistical tolerance)\n";
      return 1;
    }
  } catch (const cl::Error &error) {
    std::cerr << "OpenCL error in " << error.what() << ": " << error.err()
              << "\n";
    return 1;
  }
  std::cout << "Validation: PASS\n";
  return 0;
}
