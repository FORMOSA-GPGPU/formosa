-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local test_utils = dofile("cache_test_utils.lua")
local sequence = test_utils.sequence
local slice = test_utils.slice
local assert_bytes_equal = test_utils.assert_bytes_equal

local MEM_SIZE = 65536
local LINE_SIZE = 16
local MEM_LATENCY = 12

local CACHEABLE_LINE = 0x0000
local NON_CACHEABLE_LINE = 0x0100

local period = sc.time(1, sc.time_unit.NS)
local clock = sc.clock("clock", period)

local memory = simple.Memory("memory", {
  size = MEM_SIZE,
  latency = MEM_LATENCY,
  fifo_size = 1,
})

local cache = simtix.Cache("cache", {
  cache_size_bytes = 64,
  ways = 2,
  block_size_bytes = LINE_SIZE,
  replacement_policy = "fifo",
  write_hit_policy = "WriteBack",
  write_miss_policy = "WriteAllocate",
  mshr_entries = 2,
  mshr_subentries = 2,
  write_buffer_entries = 2,
  pipeline_queue_size = 1,
  non_cacheable_regions = {
    { addr = NON_CACHEABLE_LINE, size = LINE_SIZE },
  },
})

local initiator = simple.OutstandingInitiator("initiator")
local mmio_initiator = simple.Initiator("mmio_initiator")
local core_mon = dbg.Monitor("core_mon")
local mem_mon = dbg.Monitor("mem_mon")

initiator.target = core_mon.from
core_mon.to = cache.port
mmio_initiator.target = cache.mmio_port
cache.target = mem_mon.from
mem_mon.to = memory.port

memory.clock = clock
cache.clock = clock
mmio_initiator.clock = clock

local function wait_until_completed(target_count, max_cycles, label)
  test_utils.wait_until_completed(initiator, target_count, max_cycles, period, label)
end

local function read_cache(addr, size, label)
  local target_count = initiator:completed_count() + 1
  initiator:add_payload({ addr = addr, size = size })
  wait_until_completed(target_count, 512, label)

  local data = initiator:get_read_data()
  assert(data ~= nil, "missing read response: " .. label)
  assert(initiator:get_read_data() == nil, "unexpected extra read response")
  return data
end

local function write_cache(addr, data, label)
  local target_count = initiator:completed_count() + 1
  initiator:add_payload({ addr = addr, data = data })
  wait_until_completed(target_count, 512, label)
end

local cacheable_original = sequence(LINE_SIZE, 0x10, 3)
local cacheable_changed = sequence(LINE_SIZE, 0x80, 5)
local non_cacheable_original = sequence(LINE_SIZE, 0x20, 7)
local non_cacheable_changed = sequence(LINE_SIZE, 0xa0, 9)
local non_cacheable_patch = { 0xde, 0xad, 0xbe, 0xef }

memory:write_bytes(CACHEABLE_LINE, cacheable_original)
memory:write_bytes(NON_CACHEABLE_LINE, non_cacheable_original)

print("[NONCACHE] fill one cacheable line")
assert_bytes_equal(
  read_cache(CACHEABLE_LINE + 4, 4, "initial cacheable read"),
  slice(cacheable_original, 4, 4),
  "initial cacheable read"
)

memory:write_bytes(CACHEABLE_LINE, cacheable_changed)
assert_bytes_equal(
  read_cache(CACHEABLE_LINE + 4, 4, "cacheable hit after backing memory update"),
  slice(cacheable_original, 4, 4),
  "cacheable hit should use cached data"
)

print("[NONCACHE] non-cacheable reads observe backing memory updates")
assert_bytes_equal(
  read_cache(NON_CACHEABLE_LINE + 4, 4, "initial non-cacheable read"),
  slice(non_cacheable_original, 4, 4),
  "initial non-cacheable read"
)

memory:write_bytes(NON_CACHEABLE_LINE, non_cacheable_changed)
assert_bytes_equal(
  read_cache(NON_CACHEABLE_LINE + 4, 4, "non-cacheable read after memory update"),
  slice(non_cacheable_changed, 4, 4),
  "non-cacheable read should bypass cached state"
)

print("[NONCACHE] non-cacheable writes update backing memory")
write_cache(NON_CACHEABLE_LINE + 8, non_cacheable_patch, "non-cacheable write")
assert_bytes_equal(
  memory:read_bytes(NON_CACHEABLE_LINE + 8, #non_cacheable_patch),
  non_cacheable_patch,
  "non-cacheable write reaches memory"
)

print("[PASS] non-cacheable cache integration verified at", sc.time_stamp())
