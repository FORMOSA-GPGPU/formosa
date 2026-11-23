// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

// Checks the work-item functions, which read the work-item CSRs. PoCL writes
// only the CSRs a kernel reads, so the kernels here read all of them, one of
// them, or none.

#include <CL/opencl.hpp>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static void check(bool valid, const std::string &message) {
  if (!valid) throw std::runtime_error(message);
}

static const char *kSource = R"(
  // 7 values for each of dimensions 0 to 3, then the linear ids and the work
  // dimension.
  #define FIELDS 31
  __kernel void ids(__global ulong *out) {
    __global ulong *r = out + get_global_linear_id() * FIELDS;
    for (uint d = 0; d < 4; ++d) {
      r[d * 7 + 0] = get_global_id(d);
      r[d * 7 + 1] = get_local_id(d);
      r[d * 7 + 2] = get_group_id(d);
      r[d * 7 + 3] = get_local_size(d);
      r[d * 7 + 4] = get_num_groups(d);
      r[d * 7 + 5] = get_global_size(d);
      r[d * 7 + 6] = get_global_offset(d);
    }
    r[28] = get_global_linear_id();
    r[29] = get_local_linear_id();
    r[30] = get_work_dim();
  }

  // Reads only xlocalid_y. If PoCL did not write it, it keeps the value from
  // the previous kernel.
  __kernel void local_id_y(__global uint *counts) {
    size_t y = get_local_id(1);
    atomic_inc(&counts[y < 7 ? y : 7]);
  }

  // Reads no work-item CSR.
  __kernel void no_ids(__global ulong *out, ulong value) { out[0] = value; }
)";

constexpr size_t kFields = 31;

struct Range {
  size_t dims;
  std::array<size_t, 3> global, local, offset;
};

static cl::NDRange MakeRange(size_t dims, const std::array<size_t, 3> &v) {
  if (dims == 1) return cl::NDRange(v[0]);
  if (dims == 2) return cl::NDRange(v[0], v[1]);
  return cl::NDRange(v[0], v[1], v[2]);
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
    cl::Context context(device);
    cl::CommandQueue queue(context, device);
    cl::Program program(context, kSource);
    try {
      program.build();
    } catch (const cl::Error &) {
      std::cerr << program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(device) << '\n';
      throw;
    }

    cl::Kernel ids(program, "ids");
    const size_t max_local =
        ids.getWorkGroupInfo<CL_KERNEL_WORK_GROUP_SIZE>(device);
    // Unused dimensions have global and local size 1 and offset 0.
    const std::vector<Range> ranges = {
        {1, {12, 1, 1}, {4, 1, 1}, {0, 0, 0}},
        {1, {10, 1, 1}, {5, 1, 1}, {3, 0, 0}},
        {2, {6, 4, 1}, {3, 2, 1}, {1, 2, 0}},
        {3, {4, 3, 4}, {2, 3, 2}, {1, 2, 3}},
        {3, {8, 4, 4}, {4, 4, 4}, {0, 0, 0}},
    };
    for (const Range &r : ranges) {
      const size_t local = r.local[0] * r.local[1] * r.local[2];
      if (local > max_local) continue;
      const size_t n = r.global[0] * r.global[1] * r.global[2];
      std::vector<cl_ulong> out(n * kFields, ~cl_ulong{0});
      cl::Buffer buffer(context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                        out.size() * sizeof(cl_ulong), out.data());
      ids.setArg(0, buffer);
      queue.enqueueNDRangeKernel(ids, MakeRange(r.dims, r.offset),
                                 MakeRange(r.dims, r.global),
                                 MakeRange(r.dims, r.local));
      queue.enqueueReadBuffer(buffer, CL_TRUE, 0, out.size() * sizeof(cl_ulong),
                              out.data());
      const std::string shape =
          std::to_string(r.dims) + "D local " + std::to_string(r.local[0]) +
          "x" + std::to_string(r.local[1]) + "x" + std::to_string(r.local[2]);
      for (size_t z = 0; z < r.global[2]; ++z) {
        for (size_t y = 0; y < r.global[1]; ++y) {
          for (size_t x = 0; x < r.global[0]; ++x) {
            const std::array<size_t, 4> id = {x, y, z, 0};
            const size_t g = (z * r.global[1] + y) * r.global[0] + x;
            const cl_ulong *rec = &out[g * kFields];
            for (size_t d = 0; d < 4; ++d) {
              const size_t gs = d < 3 ? r.global[d] : 1;
              const size_t ls = d < 3 ? r.local[d] : 1;
              const size_t off = d < 3 ? r.offset[d] : 0;
              const size_t expected[7] = {
                  id[d] + off, id[d] % ls, id[d] / ls, ls, gs / ls, gs, off};
              for (size_t f = 0; f < 7; ++f)
                check(rec[d * 7 + f] == expected[f],
                      shape + ": work-item " + std::to_string(g) + " field " +
                          std::to_string(d * 7 + f) + " is " +
                          std::to_string(rec[d * 7 + f]) + ", expected " +
                          std::to_string(expected[f]));
            }
            const size_t lid =
                ((z % r.local[2]) * r.local[1] + y % r.local[1]) * r.local[0] +
                x % r.local[0];
            check(rec[28] == g && rec[29] == lid && rec[30] == r.dims,
                  shape + ": linear ids or work dimension of work-item " +
                      std::to_string(g));
          }
        }
      }
      std::cout << "PASS: ids, " << shape << '\n';
    }

    // Runs after ids, so a missing xlocalid_y write would show the old value.
    cl::Kernel local_id_y(program, "local_id_y");
    std::vector<cl_uint> counts(8, 0);
    cl::Buffer counts_buffer(context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                             counts.size() * sizeof(cl_uint), counts.data());
    local_id_y.setArg(0, counts_buffer);
    queue.enqueueNDRangeKernel(local_id_y, cl::NullRange, cl::NDRange(2, 3),
                               cl::NDRange(2, 3));
    queue.enqueueReadBuffer(counts_buffer, CL_TRUE, 0,
                            counts.size() * sizeof(cl_uint), counts.data());
    for (size_t y = 0; y < counts.size(); ++y)
      check(counts[y] == (y < 3 ? 2u : 0u),
            "local_id_y: " + std::to_string(counts[y]) +
                " work-items had local id y " + std::to_string(y));
    std::cout << "PASS: local_id_y\n";

    cl::Kernel no_ids(program, "no_ids");
    cl_ulong value = 0;
    cl::Buffer value_buffer(context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                            sizeof(value), &value);
    no_ids.setArg(0, value_buffer);
    no_ids.setArg(1, cl_ulong{0x1234});
    // One work-item, so that no two work-items write out[0].
    queue.enqueueNDRangeKernel(no_ids, cl::NullRange, cl::NDRange(1),
                               cl::NDRange(1));
    queue.enqueueReadBuffer(value_buffer, CL_TRUE, 0, sizeof(value), &value);
    check(value == 0x1234, "no_ids: wrong value");
    std::cout << "PASS: no_ids\n";
  } catch (const cl::Error &error) {
    std::cerr << error.what() << ": " << error.err() << '\n';
    return 1;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
