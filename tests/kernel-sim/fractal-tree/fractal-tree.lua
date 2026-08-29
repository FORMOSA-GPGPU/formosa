-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local ffi = require("ffi")
local util = require("lv.util")

---@type kernel-sim.runtime
local ksim = require("kernel-sim.runtime")

---@type kernel-sim.system
local system = require("kernel-sim.system")("fractal-tree.lua", { ... })

-- Kernel argument definition
ffi.cdef([[
typedef struct {
  int x1, y1;
  int x2, y2;
} branch_t;

typedef struct {
  branch_t *branches;
  int *SIN_TABLE;
  int *COS_TABLE;
  int table_size;
  int SHIFT;

  int total_branches;
  int initial_len;
  int L_angle;
  int R_angle;
  int L_shrink;
  int R_shrink;
} kargs_t;
]])

-- Fixed-point settings
local SHIFT = 10
local SCALE = math.pow(2, SHIFT)

-- Fractal tree settings
local max_depth = 10
local total_branches = math.pow(2, max_depth) - 1

local initial_len = 140
local L_angle = 30 -- degrees
local R_angle = 60 -- degrees
local L_shrink = math.floor(0.77 * SCALE)
local R_shrink = math.floor(0.65 * SCALE)

-- Allocate memory for branches of the fractal tree
local branches_addr = ksim.scratch_base
local branches = ffi.new("branch_t[?]", total_branches)
ksim.upload_data(system, branches_addr, branches)

-- Allocate memory for sin/cos tables and compute values in Q10
local sin_addr = ksim.scratch_base + 0x8000
local cos_addr = ksim.scratch_base + 0x9000

local table_size = 360
local deg_step = 360 / table_size
local sin_table = ffi.new("int[?]", table_size)
local cos_table = ffi.new("int[?]", table_size)

for i = 0, table_size - 1 do
  local rad = (i * deg_step) * math.pi / 180.0
  sin_table[i] = math.floor(math.sin(rad) * SCALE)
  cos_table[i] = math.floor(math.cos(rad) * SCALE)
end

assert(sin_addr >= branches_addr + total_branches * ffi.sizeof("branch_t"))
assert(cos_addr >= sin_addr + table_size * ffi.sizeof("int"))
ksim.upload_data(system, sin_addr, sin_table)
ksim.upload_data(system, cos_addr, cos_table)

-- Allocate memory for kernel arguments
local kargs_addr = ksim.scratch_base + 0xA000
local kargs = ffi.new("kargs_t", {
  branches = ffi.cast("branch_t*", branches_addr),
  SIN_TABLE = ffi.cast("int*", sin_addr),
  COS_TABLE = ffi.cast("int*", cos_addr),
  table_size = table_size,
  SHIFT = SHIFT,

  total_branches = total_branches,
  initial_len = initial_len,
  L_angle = L_angle,
  R_angle = R_angle,
  L_shrink = L_shrink,
  R_shrink = R_shrink,
})

assert(kargs_addr >= cos_addr + table_size * ffi.sizeof("int"))
ksim.upload_data(system, kargs_addr, kargs)

-- Start to run kernel
ksim.run_kernel(
  system,
  util.runfile("bin/fractal-tree.kernel-sim.elf"),
  kargs_addr,
  total_branches,
  system.num_threads
)

-- Download the result
local result = ffi.new("branch_t[?]", total_branches)
local result_ptr = ffi.cast("uint8_t*", result)

local total_bytes = total_branches * ffi.sizeof("branch_t")
local chunk_size = 2048
local temp_buf = ffi.new("uint8_t[?]", chunk_size)

for i = 0, total_bytes - 1, chunk_size do
  local size = math.min(chunk_size, total_bytes - i)
  local buf = temp_buf
  -- If the last chunk is smaller, create a specific buffer for it
  if size ~= chunk_size then buf = ffi.new("uint8_t[?]", size) end

  ksim.download_data(system, branches_addr + i, buf)
  ffi.copy(result_ptr + i, buf, size)
end

-- Calculate golden result
local function fractal_tree(idx, x, y, angle, len, depth)
  if depth >= max_depth then return end
  if idx < 0 or idx >= total_branches then return end

  -- Calculate end point
  local n_ang = (angle % 360 + 360) % 360
  local dx = bit.arshift(cos_table[n_ang] * len, SHIFT)
  local dy = bit.arshift(sin_table[n_ang] * len, SHIFT)
  local x2, y2 = x + dx, y + dy

  -- Store the branch information
  local golden_x1 = bit.arshift(x, SHIFT)
  local golden_y1 = bit.arshift(y, SHIFT)
  local golden_x2 = bit.arshift(x2, SHIFT)
  local golden_y2 = bit.arshift(y2, SHIFT)

  print(
    string.format(
      "Branch %d: (%.1f, %.1f) to (%.1f, %.1f)",
      idx,
      golden_x1,
      golden_y1,
      golden_x2,
      golden_y2
    )
  )

  assert(
    golden_x1 == result[idx].x1
      and golden_y1 == result[idx].y1
      and golden_x2 == result[idx].x2
      and golden_y2 == result[idx].y2
  )

  -- Keep generating left and right subtrees
  local left_idx = 2 * idx + 1
  local left_len = bit.arshift(len * L_shrink, SHIFT)
  local right_idx = 2 * idx + 2
  local right_len = bit.arshift(len * R_shrink, SHIFT)
  fractal_tree(left_idx, x2, y2, angle + L_angle, left_len, depth + 1)
  fractal_tree(right_idx, x2, y2, angle - R_angle, right_len, depth + 1)
end

fractal_tree(0, 0, 0, 90, initial_len * SCALE, 0)

-- Generate SVG file
--[[
print("Generating SVG file...")
local svg_file = io.open("fractal_tree.svg", "w")
local width, height = 800, 600
local offset_x = width / 2
local offset_y = height - 50

svg_file:write(string.format('<svg width="%d" height="%d" xmlns="http://www.w3.org/2000/svg" style="background:#f0f0f0">\n', width, height))

for i = 0, total_branches - 1 do
  local b = result[i]

  -- Transform coordinates to fit SVG canvas
  local x1 = b.x1 + offset_x
  local y1 = offset_y - b.y1
  local x2 = b.x2 + offset_x
  local y2 = offset_y - b.y2

  -- Apply color and stroke width based on depth
  local depth = math.floor(math.log(i + 1, 2))
  local stroke_width = math.max(0.5, max_depth - depth)
  local color_val = math.min(200, depth * 20)
  local color = string.format("rgb(%d,%d,%d)", color_val, color_val, color_val)

  svg_file:write(string.format(
    '  <line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="%s" stroke-width="%.1f" stroke-linecap="round" />\n',
    x1, y1, x2, y2, color, stroke_width
  ))
end

svg_file:write("</svg>\n")
svg_file:close()
--]]

print("Pass!")
print(system.stats)
