-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local bit = require("bit")
local argparse = require("argparse")

-- Parameters
local parser = argparse("lsu.lua")
parser:option("--lsu", "The LSU to test"):default("simple")
parser:option("--num-lanes", "Number of lanes"):default(32):convert(tonumber)
parser:option("--total-iter", "Total number of iterations"):default(200):convert(tonumber)
parser:option("--seed", "Random seed"):default(42):convert(tonumber)
parser:option("--mem-size", "Memory size in bytes"):default(65536):convert(tonumber)

local args = parser:parse({ ... })

local NUM_LANES = args.num_lanes
local MEM_SIZE = args.mem_size
local TOTAL_ITER = args.total_iter
local SEED = args.seed

local param = {
  num_lanes = NUM_LANES,
}

local tester = simtix.LsuTester("tester", param)
tester:lsu_init(function(name)
  if args.lsu == "simple" then
    return simtix.SimpleLsu(name, param)
  elseif args.lsu == "coalescing" then
    return simtix.CoalescingLsu(name, param, {
      enable_stack_remap = false, -- Disable stack remap for testing
    })
  end
end)

local mem = simple.Memory("mem", {
  size = MEM_SIZE,
})
local period = sc.time(10, sc.time_unit.NS)
local clock = sc.clock("clock", period)

tester.clock = clock
mem.clock = clock
tester.target = mem.port

sc.start(sc.ZERO_TIME)

-- Randomly fill the memory blocks
for addr = 0, MEM_SIZE - 1 do
  mem:write_bytes(addr, { math.random(0, 255) })
end

-- Use a fixed seed for reproducibility in CI
math.randomseed(SEED)

local function is_active(tmask, lane_id)
  lane_id = #tmask - lane_id + 1
  return tmask:sub(lane_id, lane_id) == "1"
end

local function align(addr, width) return addr - (addr % width) end

local widths = { 1, 2, 4, 8 }

for iter = 1, TOTAL_ITER do
  local width = widths[math.random(1, #widths)]
  local is_store = math.random(0, 1) == 1
  local tmask = ""
  for _ = 1, NUM_LANES do
    tmask = tostring(math.random(0, 1)) .. tmask
  end

  local addr = {}
  local pattern_type = math.random(1, 3) -- 1: Uniform, 2: Stride, 3: Random

  -- Force non-uniform for stores
  if is_store and pattern_type == 1 then pattern_type = math.random(2, 3) end

  if pattern_type == 1 then
    -- Uniform pattern
    local base = align(math.random(0, MEM_SIZE - width), width)
    for _ = 1, NUM_LANES do
      table.insert(addr, base)
    end
  elseif pattern_type == 2 then
    -- Stride pattern
    local stride
    if is_store then
      -- For stores, stride must be non-zero to avoid duplicates
      local strides = { -2, -1, 1, 2 }
      stride = strides[math.random(1, #strides)] * width
    else
      stride = math.random(-2, 2) * width
    end

    local base
    if stride >= 0 then
      base = align(math.random(0, MEM_SIZE - width - (NUM_LANES - 1) * stride), width)
    else
      base = align(math.random(-(NUM_LANES - 1) * stride, MEM_SIZE - width), width)
    end

    for i = 1, NUM_LANES do
      table.insert(addr, base + (i - 1) * stride)
    end
  else
    -- Random pattern
    local used_addr = {}
    for _ = 1, NUM_LANES do
      local lane_addr
      repeat
        lane_addr = align(math.random(0, MEM_SIZE - width), width)
      until not is_store or not used_addr[lane_addr]
      table.insert(addr, lane_addr)
      used_addr[lane_addr] = true
    end
  end

  local ip = iter * 4
  local pattern_names = { "Uniform", "Stride", "Random" }

  if is_store then
    local data = {}
    local lane_bytes = {}
    for i = 1, NUM_LANES do
      local bytes = {}
      for j = 1, 8 do
        local b = math.random(0, 255)
        bytes[j] = b
        table.insert(data, b)
      end
      lane_bytes[i] = bytes
    end

    print(
      string.format(
        "Iteration %d (Store, %s): tmask=%s, width=%d",
        iter,
        pattern_names[pattern_type],
        tmask,
        width
      )
    )
    tester:store(ip, addr, data, width, tmask)

    -- Validate results for active lanes
    for i = 1, NUM_LANES do
      if is_active(tmask, i) then
        local actual = mem:read_bytes(addr[i], width)
        for j = 1, width do
          print(
            string.format("0x%x[%d]: 0x%02x == 0x%02x", addr[i], j - 1, lane_bytes[i][j], actual[j])
          )
          assert(lane_bytes[i][j] == actual[j])
        end
      end
    end
  else
    local is_signed = math.random(0, 1) == 1
    print(
      string.format(
        "Iteration %d (Load, %s): tmask=%s, width=%d, is_signed=%s",
        iter,
        pattern_names[pattern_type],
        tmask,
        width,
        tostring(is_signed)
      )
    )
    local result = tester:load(ip, addr, width, is_signed, tmask)

    -- Validate results for active lanes
    for i = 1, NUM_LANES do
      if is_active(tmask, i) then
        local golden = mem:read_bytes(addr[i], width)
        local sign_byte = 0
        if is_signed and bit.band(golden[width], 0x80) ~= 0 then sign_byte = 0xff end

        for j = 1, 8 do
          local index = (i - 1) * 8 + j
          local expected = (j <= width) and golden[j] or sign_byte
          print(
            string.format("0x%x[%d]: 0x%02x == 0x%02x", addr[i], j - 1, expected, result[index])
          )
          assert(expected == result[index])
        end
      end
    end
  end
end

lv.info("Pass!")
