-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local ffi = require("ffi")
local util = require("lv.util")

---@type kernel-sim.runtime
local ksim = require("kernel-sim.runtime")

---@type kernel-sim.system
local system = require("kernel-sim.system")("oesort.lua", { ... })

-- The kernel argument definition.
ffi.cdef([[
typedef struct {
  int *arr;
  int n;
} kargs_t;
]])

local n = 100

-- Allocate an integer array for test data.
local arr_addr = ksim.scratch_base
local arr = ffi.new("int[?]", n)
for i = 0, n - 1 do
  arr[i] = math.random(-100, 100) -- Randomly generate data array.
end
ksim.upload_data(system, arr_addr, arr)

-- Allocate a space for kernel argument.
local kargs_addr = ksim.scratch_base + 0xa000
assert(kargs_addr >= arr_addr + n * ffi.sizeof("int")) -- Make sure kargs is not overlapping.
local kargs = ffi.new("kargs_t", {
  arr = ffi.cast("int*", arr_addr), -- Using the allocated address.
  n = n,
})
ksim.upload_data(system, kargs_addr, kargs)

-- Start the kernel with the kernel argument, global size and local size, which is set to the max
-- number of threads per core.
ksim.run_kernel(
  system,
  util.runfile("bin/oesort.kernel-sim.elf"),
  kargs_addr,
  n,
  system.num_threads
)

-- Download the array data. The computation is done in-place.
local arr_result = ffi.new("int[?]", n)
ksim.download_data(system, arr_addr, arr_result)

-- Check if oesort is calculated correctly.
for i = 0, n - 1 do
  print(i, arr[i], arr_result[i])
  if i + 1 < n then assert(arr_result[i] <= arr_result[i + 1]) end
end

print("Pass!")
print(system.stats)
