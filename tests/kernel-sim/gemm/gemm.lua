-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local ffi = require("ffi")
local util = require("lv.util")

---@type kernel-sim.runtime
local ksim = require("kernel-sim.runtime")

---@type kernel-sim.system
local system = require("kernel-sim.system")("gemm.lua", { ... })

-- The kernel argument definition.
ffi.cdef([[
typedef struct {
  int *A_arr;  // m x k
  int *B_arr;  // k x n
  int *C_arr;  // m x n
  int alpha;
  int beta;
  int m;
  int k;
  int n;
} kargs_t;
]])
-- Make sure different testcases in every execution.
math.randomseed(os.time())

local alpha = math.random(-100, 100)
local beta = math.random(-100, 100)
local m = math.random(1, 10)
local n = math.random(1, 10)
local k = math.random(1, 10)

-- Allocate an integer array for test data.
local arr_addr = ksim.scratch_base
local A_size = m * k * ffi.sizeof("int")
local B_size = k * n * ffi.sizeof("int")
local C_size = m * n * ffi.sizeof("int")
local A_addr = arr_addr
local B_addr = arr_addr + A_size
local C_addr = arr_addr + A_size + B_size
local A_arr = ffi.new("int[?]", m * k)
local B_arr = ffi.new("int[?]", k * n)
local C_arr = ffi.new("int[?]", m * n)
for i = 0, (m * k) - 1 do
  A_arr[i] = math.random(-100, 100) -- Randomly generate data array.
end
for i = 0, (k * n) - 1 do
  B_arr[i] = math.random(-100, 100) -- Randomly generate data array.
end
for i = 0, (m * n) - 1 do
  C_arr[i] = math.random(-100, 100) -- Randomly generate data array.
end
ksim.upload_data(system, A_addr, A_arr)
ksim.upload_data(system, B_addr, B_arr)
ksim.upload_data(system, C_addr, C_arr)

-- Allocate a space for kernel argument.
local kargs_addr = ksim.scratch_base + 0xa000
assert(kargs_addr >= C_addr + C_size) -- Make sure kargs is not overlapping.
local kargs = ffi.new("kargs_t", {
  A_arr = ffi.cast("int*", A_addr), -- Using the allocated address.
  B_arr = ffi.cast("int*", B_addr), -- Using the allocated address.
  C_arr = ffi.cast("int*", C_addr), -- Using the allocated address.
  alpha = alpha,
  beta = beta,
  m = m,
  k = k,
  n = n,
})
ksim.upload_data(system, kargs_addr, kargs)

-- Start the kernel with the kernel argument, global size and local size, which is set to the max
-- number of threads per core.
ksim.run_kernel(
  system,
  util.runfile("bin/gemm.kernel-sim.elf"),
  kargs_addr,
  m * n,
  system.num_threads
)

-- Download the array data. The computation is done in C_addr
local arr_result = ffi.new("int[?]", m * n)
ksim.download_data(system, C_addr, arr_result)

-- Check if gemm is calculated correctly.
print(
  string.format(
    "Testing C[%2d][%2d] = (%d) * (A[%2d][%2d] * B[%2d][%2d]) + (%d) * C[%2d][%2d]",
    m,
    n,
    alpha,
    m,
    k,
    k,
    n,
    beta,
    m,
    n
  )
)
for i = 0, m - 1 do
  for j = 0, n - 1 do
    local expected = beta * C_arr[i * n + j]

    -- Calculate dot product of Row A[i] and Col B[j]
    for p = 0, k - 1 do
      local val_a = A_arr[i * k + p]
      local val_b = B_arr[p * n + j]
      expected = expected + alpha * (val_a * val_b)
    end

    local actual = arr_result[i * n + j]
    local status = (actual == expected) and "pass" or "fail"
    print(
      string.format("answer[%2d][%2d] = %8d, expected = %8d, %s", i, j, actual, expected, status)
    )
    assert(expected == actual)
  end
end

print("Pass!")
print(system.stats)
