-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local ffi = require("ffi")
local util = require("lv.util")

---@type kernel-sim.runtime
local ksim = require("kernel-sim.runtime")

---@type kernel-sim.system
local system = require("kernel-sim.system")("convolution.lua", { ... })

ffi.cdef([[
typedef struct {
  int        *input;
  int        *weight;
  long long  *result;
  int         width;
  int         height;
} kargs_t;
]])

local width = 16
local height = 16
local img_n = width * height
local kernel_size = 5 * 5

-- Input feature map
local input_addr = ksim.scratch_base
local input_host = ffi.new("int[?]", img_n)
for i = 0, img_n - 1 do
  input_host[i] = math.random(-5, 5)
end
ksim.upload_data(system, input_addr, input_host)

local input_before = ffi.new("int[?]", img_n)
ffi.copy(input_before, input_host, img_n * ffi.sizeof("int"))

-- Kernel weights
local weight_addr = input_addr + img_n * ffi.sizeof("int")
local weight_host = ffi.new("int[?]", kernel_size)
for i = 0, kernel_size - 1 do
  weight_host[i] = math.random(-3, 3)
end
ksim.upload_data(system, weight_addr, weight_host)

local function align_up(addr, align) return math.floor((addr + align - 1) / align) * align end
local weight_end = weight_addr + kernel_size * ffi.sizeof("int")
local result_addr = align_up(weight_end, 8)
local result_host = ffi.new("long long[?]", img_n)
for i = 0, img_n - 1 do
  result_host[i] = 0LL
end
ksim.upload_data(system, result_addr, result_host)

-- Kernel args
local result_end = result_addr + img_n * ffi.sizeof("long long")
local kargs_addr = align_up(result_end, 8)
assert(kargs_addr >= result_addr + img_n * ffi.sizeof("long long"))
local kargs = ffi.new("kargs_t", {
  input = ffi.cast("int*", input_addr),
  weight = ffi.cast("int*", weight_addr),
  result = ffi.cast("long long*", result_addr),
  width = width,
  height = height,
})
ksim.upload_data(system, kargs_addr, kargs)

-- Run kernel
local global_size = img_n
ksim.run_kernel(
  system,
  util.runfile("bin/convolution.kernel-sim.elf"),
  kargs_addr,
  global_size,
  system.num_threads
)

-- Download result
local result_sim = ffi.new("long long[?]", img_n)
ksim.download_data(system, result_addr, result_sim)

-- Golden reference
local function conv5x5_ref(input, weight, w, h)
  local out = ffi.new("long long[?]", w * h)

  for x = 0, h - 1 do
    for y = 0, w - 1 do
      local acc = 0LL
      for i = x - 2, x + 2 do
        for j = y - 2, y + 2 do
          local x_index = i - x + 2
          local y_index = j - y + 2
          local m = y_index + x_index * 5

          if i >= 0 and i < h and j >= 0 and j < w then
            acc = acc + (input[i * w + j] * weight[m])
          end
        end
      end
      out[x * w + y] = acc
    end
  end

  return out
end

local golden = conv5x5_ref(input_before, weight_host, width, height)

for i = 0, img_n - 1 do
  local g = tonumber(golden[i])
  local hw = tonumber(result_sim[i])
  print(i, input_before[i], g, hw)

  assert(hw == g)
end

print("Pass!")
print(system.stats)
