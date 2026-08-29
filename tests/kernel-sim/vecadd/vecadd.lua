-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local ffi = require("ffi")
local util = require("lv.util")

---@type kernel-sim.runtime
local ksim = require("kernel-sim.runtime")

---@type kernel-sim.system
local system = require("kernel-sim.system")("vecadd.lua", { ... })

-- The kernel argument definition.
ffi.cdef([[
typedef struct {
  int *a;
  int *b;
  int *c;
  int n;
} kargs_t;
]])

local n = 100

-- Add random seed for reproducibility
math.randomseed(os.time())

-- Allocate integer arrays for test data.
local a_addr = ksim.scratch_base
local b_addr = ksim.scratch_base + 0x2000
local c_addr = ksim.scratch_base + 0x4000

local a = ffi.new("int[?]", n)
local b = ffi.new("int[?]", n)

for i = 0, n - 1 do
  a[i] = math.random(-100000, 100000)
  b[i] = math.random(-100000, 100000)
end

ksim.upload_data(system, a_addr, a)
ksim.upload_data(system, b_addr, b)

-- Allocate a space for kernel argument.
local kargs_addr = ksim.scratch_base + 0x6000
assert(kargs_addr >= c_addr + n * ffi.sizeof("int")) -- Make sure kargs is not overlapping.
local kargs = ffi.new("kargs_t", {
  a = ffi.cast("int*", a_addr),
  b = ffi.cast("int*", b_addr),
  c = ffi.cast("int*", c_addr),
  n = n,
})
ksim.upload_data(system, kargs_addr, kargs)

-- Start the kernel with the kernel argument, global size and local size.
ksim.run_kernel(
  system,
  util.runfile("bin/vecadd.kernel-sim.elf"),
  kargs_addr,
  n,
  system.num_threads
)

-- Download the result.
local c_result = ffi.new("int[?]", n)
ksim.download_data(system, c_addr, c_result)

-- Check if vecadd is calculated correctly.
for i = 0, n - 1 do
  local expected = a[i] + b[i]
  print(i, a[i], b[i], c_result[i])
  assert(c_result[i] == expected)
end
print("Pass!")
print(system.stats)
