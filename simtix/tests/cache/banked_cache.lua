-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- Addresses that differ only in the interleaved bank bits must occupy
-- independent banks and different sets of the same bank. Without
-- preserve-original-address plus a derived set-index shift, 0x00 and 0x100
-- collide in one set of bank 0 and 0x100 evicts 0x00.

local BankedCache = require("simtix.banked_cache")
local test_utils = dofile("cache_test_utils.lua")

local MEM_SIZE = 65536
local BLOCK = 64
local period = sc.time(1, sc.time_unit.NS)

local ADDR_SET0 = 0x00
local ADDR_SET1 = 0x100
local ADDR_SET0_CONFLICT = 0x200
local DATA_SET0 = { 0xa1, 0xa2, 0xa3, 0xa4 }
local DATA_SET1 = { 0xb1, 0xb2, 0xb3, 0xb4 }
local DATA_CONFLICT = { 0xc1, 0xc2, 0xc3, 0xc4 }
local ZERO = { 0x00, 0x00, 0x00, 0x00 }

local initiator = simple.Initiator("initiator")
local mmio_initiator = simple.Initiator("mmio_initiator")
local memory = simple.Memory("memory", { size = MEM_SIZE, latency = 4 })
local cache = BankedCache("cache", {
  cache_size_bytes = 512,
  block_size_bytes = BLOCK,
  num_banks = 4,
  ways = 1,
  write_hit_policy = "WriteBack",
  write_miss_policy = "WriteAllocate",
  replacement_policy = "fifo",
})

initiator.target = cache.port
mmio_initiator.target = cache.mmio_port
cache.target = memory.port

local clock = sc.clock("clock", period)
initiator.clock = clock
mmio_initiator.clock = clock
cache.clock = clock
memory.clock = clock

local function write_line(addr, data, label)
  local target = initiator:completed_count() + 1
  initiator:add_payload({ addr = addr, data = data })
  test_utils.wait_until_completed(initiator, target, 1024, period, label)
  sc.start(256 * period)
end

write_line(ADDR_SET0, DATA_SET0, "set0")
write_line(ADDR_SET1, DATA_SET1, "set1")

test_utils.assert_bytes_equal(
  memory:read_bytes(ADDR_SET0, 4),
  ZERO,
  "0x00 must stay dirty in bank 0 set 0"
)
test_utils.assert_bytes_equal(
  memory:read_bytes(ADDR_SET1, 4),
  ZERO,
  "0x100 must stay dirty in bank 0 set 1"
)

write_line(ADDR_SET0_CONFLICT, DATA_CONFLICT, "set0 conflict")

test_utils.assert_bytes_equal(
  memory:read_bytes(ADDR_SET0, 4),
  DATA_SET0,
  "conflicting line must write back 0x00"
)
test_utils.assert_bytes_equal(
  memory:read_bytes(ADDR_SET1, 4),
  ZERO,
  "0x100 must remain cached in the other set"
)
test_utils.assert_bytes_equal(
  memory:read_bytes(ADDR_SET0_CONFLICT, 4),
  ZERO,
  "0x200 must stay dirty after replacing 0x00"
)

print("Pass!")
