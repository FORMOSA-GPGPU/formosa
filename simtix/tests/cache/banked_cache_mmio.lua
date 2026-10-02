-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local BankedCache = require("simtix.banked_cache")
local test_utils = dofile("cache_test_utils.lua")

local MEM_SIZE = 65536
local MMIO_BASE = 0
local period = sc.time(1, sc.time_unit.NS)
local BLOCK = 64
local MMIO_START_OFF = 0x0
local MMIO_ADDR_OFF = 0x8
local MMIO_SIZE_OFF = 0x10
local MMIO_OP_OFF = 0x18
local MMIO_OP_FLUSH = 1

local RANGE_START = 0x00
local RANGE_SIZE = 2 * BLOCK
local IN_RANGE = { 0x00, 0x40 }
local OUT_RANGE = { 0x80, 0xC0 }
local ZERO = { 0x00, 0x00, 0x00, 0x00 }
local DATA_IN = {
  { 0xa1, 0xa2, 0xa3, 0xa4 },
  { 0xb1, 0xb2, 0xb3, 0xb4 },
}
local DATA_OUT = {
  { 0xc1, 0xc2, 0xc3, 0xc4 },
  { 0xd1, 0xd2, 0xd3, 0xd4 },
}

local initiator = simple.Initiator("initiator")
local mmio_initiator = simple.Initiator("mmio_initiator")
local memory = simple.Memory("memory", { size = MEM_SIZE, latency = 4 })
local cache = BankedCache("cache", {
  cache_size_bytes = 4096,
  block_size_bytes = BLOCK,
  num_banks = 4,
  ways = 4,
  write_hit_policy = "WriteBack",
  write_miss_policy = "WriteAllocate",
  replacement_policy = "lru",
})

initiator.target = cache.port
mmio_initiator.target = cache.mmio_port
cache.target = memory.port

local clock = sc.clock("clock", period)
initiator.clock = clock
mmio_initiator.clock = clock
cache.clock = clock
memory.clock = clock

local function mmio_write(offset, value, label)
  test_utils.mmio_write_u64(mmio_initiator, MMIO_BASE, offset, value, period, 512, label)
end

local function wait_idle(label)
  test_utils.wait_mmio_idle(mmio_initiator, MMIO_BASE, MMIO_START_OFF, period, 4000, label)
end

local function write_line(addr, data)
  local target = initiator:completed_count() + 1
  initiator:add_payload({ addr = addr, data = data })
  test_utils.wait_until_completed(initiator, target, 512, period, "dirty write")
end

for i, addr in ipairs(IN_RANGE) do
  write_line(addr, DATA_IN[i])
end
for i, addr in ipairs(OUT_RANGE) do
  write_line(addr, DATA_OUT[i])
end

for _, addr in ipairs({ 0x00, 0x40, 0x80, 0xC0 }) do
  assert(
    test_utils.bytes_equal(memory:read_bytes(addr, 4), ZERO),
    "backing memory must stay clean until flush"
  )
end

mmio_write(MMIO_ADDR_OFF, RANGE_START, "addr")
mmio_write(MMIO_SIZE_OFF, RANGE_SIZE, "size")
mmio_write(MMIO_OP_OFF, MMIO_OP_FLUSH, "op")
mmio_write(MMIO_START_OFF, 1, "start")
wait_idle("ranged flush")

for i, addr in ipairs(IN_RANGE) do
  test_utils.assert_bytes_equal(
    memory:read_bytes(addr, 4),
    DATA_IN[i],
    string.format("in-range 0x%x flushed", addr)
  )
end
for _, addr in ipairs(OUT_RANGE) do
  test_utils.assert_bytes_equal(
    memory:read_bytes(addr, 4),
    ZERO,
    string.format("out-of-range 0x%x must not flush", addr)
  )
end

print("Pass!")
