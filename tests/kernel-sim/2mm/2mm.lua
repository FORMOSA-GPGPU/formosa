-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local ffi = require("ffi")
local util = require("lv.util")

---@type kernel-sim.runtime
local ksim = require("kernel-sim.runtime")

---@type kernel-sim.system
local system = require("kernel-sim.system")("2mm.lua", { ... })

-- The kernel argument definition.
ffi.cdef([[
typedef struct {
  int *A;
  int *B;
  int *C;
  int ni;
  int nj;
  int nk;
} kargs_t;
]])

-- Matrix Dimensions (Set matrix size: ni * nk, nk * nj -> ni * nj)
local ni = 8
local nj = 8
local nk = 16

-- Memory Address Allocation (Manually plan memory locations to avoid overlap)
local base_addr = ksim.scratch_base
local size_int = ffi.sizeof("int")
local addr_A = base_addr
local addr_B = addr_A + (ni * nk * size_int)
local addr_C = addr_B + (nk * nj * size_int)
local addr_kargs = addr_C + (ni * nj * size_int) + 0x100 -- Padding

-- Helper function to print a matrix
local function print_matrix(name, arr, rows, cols)
  print(string.format("--- %s (%dx%d) ---", name, rows, cols))
  for r = 0, rows - 1 do
    local line = ""
    for c = 0, cols - 1 do
      -- %4d ensures numbers are aligned with 4 spaces
      line = line .. string.format("%4d ", arr[r * cols + c])
    end
    print(line)
  end
  print("") -- Empty line
end

-- Prepare Data: Matrix A
local arr_A = ffi.new("int[?]", ni * nk)
for i = 0, (ni * nk) - 1 do
  arr_A[i] = math.random(-5, 5) -- Random small integers
end
ksim.upload_data(system, addr_A, arr_A)

-- Prepare Data: Matrix B
local arr_B = ffi.new("int[?]", nk * nj)
for i = 0, (nk * nj) - 1 do
  arr_B[i] = math.random(-5, 5)
end
ksim.upload_data(system, addr_B, arr_B)

-- Prepare Data: Matrix C (Initialized to 0, though output will overwrite)
local arr_C = ffi.new("int[?]", ni * nj)
for i = 0, (ni * nj) - 1 do
  arr_C[i] = 0
end
ksim.upload_data(system, addr_C, arr_C)

-- [Visual Check 1] Print Input Matrices
print_matrix("Input Matrix A", arr_A, ni, nk)
print_matrix("Input Matrix B", arr_B, nk, nj)

-- Prepare Kernel Arguments
local kargs = ffi.new("kargs_t", {
  A = ffi.cast("int*", addr_A),
  B = ffi.cast("int*", addr_B),
  C = ffi.cast("int*", addr_C),
  ni = ni,
  nj = nj,
  nk = nk,
})
ksim.upload_data(system, addr_kargs, kargs)

print(
  string.format(
    "Running Kernel... Matrix Size: %dx%d * %dx%d. Total Threads: %d\n",
    ni,
    nk,
    nk,
    nj,
    system.num_threads
  )
)

-- Start the kernel
-- Here num_threads passed must be greater than or equal to ni * nj
ksim.run_kernel(
  system,
  util.runfile("bin/2mm.kernel-sim.elf"),
  addr_kargs,
  ni * nj,
  system.num_threads
)

-- Download the result Matrix C
local result_C_gpu = ffi.new("int[?]", ni * nj)
ksim.download_data(system, addr_C, result_C_gpu)

-- [Visual Check 2] Print Output Matrix
print_matrix("Output Matrix C (GPU Result)", result_C_gpu, ni, nj)

-- Verification (CPU Golden Check)
print("Verifying results...")
local pass = true

for i = 0, ni - 1 do
  for j = 0, nj - 1 do
    -- Calculate expected value (CPU)
    local sum_cpu = 0
    for k = 0, nk - 1 do
      sum_cpu = sum_cpu + arr_A[i * nk + k] * arr_B[k * nj + j]
    end

    -- Compare with GPU result
    local idx = i * nj + j
    local val_gpu = result_C_gpu[idx]

    if val_gpu ~= sum_cpu then
      print(string.format("Mismatch at [%d][%d]: CPU=%d, GPU=%d", i, j, sum_cpu, val_gpu))
      pass = false
    end
  end
end

if pass then
  print("Pass!")
else
  print("Fail!")
  assert(false)
end
print(system.stats)
