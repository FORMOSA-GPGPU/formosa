-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local ffi = require("ffi")
local util = require("lv.util")

---@type kernel-sim.runtime
local ksim = require("kernel-sim.runtime")

---@type kernel-sim.system
local system = require("kernel-sim.system")("voronoi.lua", { ... })

-- The kernel argument definition.
ffi.cdef([[
typedef struct {
  int x, y;
  int color;
} seed_t;

typedef struct {
  int *output_image;
  seed_t *seeds;
  int num_seeds;
  int width;
  int height;
} kargs_t;
]])

-- Add random seed for reproducibility
math.randomseed(os.time())

-- Image setting
local width, height = 64, 64
local num_seeds = 12
local total_pixels = width * height

-- Allocate integer array for image
local img_addr = ksim.scratch_base
local seeds_addr = ksim.scratch_base + 0xa000

local imgs = ffi.new("int[?]", total_pixels)
local seeds = ffi.new("seed_t[?]", num_seeds)

for i = 0, num_seeds - 1 do
  seeds[i].x = math.random(5, width - 5)
  seeds[i].y = math.random(5, height - 5)
  seeds[i].color = math.random(0, 0xFFFFFF)
end

assert(seeds_addr >= img_addr + total_pixels * ffi.sizeof("int")) -- Make sure seeds is not overlapping
ksim.upload_data(system, img_addr, imgs)
ksim.upload_data(system, seeds_addr, seeds)

-- Allocate a space for kernel argument.
local kargs_addr = ksim.scratch_base + 0xb000
assert(kargs_addr >= seeds_addr + num_seeds * ffi.sizeof("seed_t")) -- Make sure kargs is not overlapping.

local kargs = ffi.new("kargs_t", {
  output_image = ffi.cast("int*", img_addr),
  seeds = ffi.cast("seed_t*", seeds_addr),
  num_seeds = num_seeds,
  width = width,
  height = height,
})

ksim.upload_data(system, kargs_addr, kargs)

-- Start to run the kernel
ksim.run_kernel(
  system,
  util.runfile("bin/voronoi.kernel-sim.elf"),
  kargs_addr,
  total_pixels,
  system.num_threads
)

-- Download the result image
local result_img = ffi.new("int[?]", total_pixels)
local result_ptr = ffi.cast("uint8_t*", result_img)

local total_bytes = total_pixels * ffi.sizeof("int")
local chunk_size = 2048
local temp_buf = ffi.new("uint8_t[?]", chunk_size)

for i = 0, total_bytes - 1, chunk_size do
  local size = math.min(chunk_size, total_bytes - i)
  local buf = temp_buf
  -- If the last chunk is smaller, create a specific buffer for it
  if size ~= chunk_size then buf = ffi.new("uint8_t[?]", size) end

  ksim.download_data(system, img_addr + i, buf)
  ffi.copy(result_ptr + i, buf, size)
end

-- Calculate golden result
for py = 0, height - 1 do
  for px = 0, width - 1 do
    local min_dist = 2147483647
    local golden_color = 0

    for i = 0, num_seeds - 1 do
      local dx = px - seeds[i].x
      local dy = py - seeds[i].y
      local dist = dx * dx + dy * dy

      if dist == 0 then
        golden_color = 0 -- The pixel is exactly on a seed point
        break
      end

      if dist < min_dist then
        min_dist = dist
        golden_color = seeds[i].color
      end
    end

    local pixel = py * width + px
    assert(result_img[pixel] == golden_color)
  end
end

-- Save the result image as PPM file
--[[
local f = io.open("voronoi.ppm", "w")
f:write("P3\n", width, " ", height, "\n255\n")
for i = 0, total_pixels - 1 do
  local val = result_img[i]
  local b = val % 256
  local g = math.floor(val / 256) % 256
  local r = math.floor(val / 65536) % 256
  f:write(r, " ", g, " ", b, " ")
end
f:close()
--]]

print("Pass!")
print(system.stats)
