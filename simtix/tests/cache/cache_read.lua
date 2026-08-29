-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local test_utils = dofile("cache_test_utils.lua")
local clone = test_utils.clone
local sequence = test_utils.sequence
local slice = test_utils.slice

local MEM_SIZE = 65536
local LINE_SIZE = 64
local MEM_LATENCY = 24

local period = sc.time(1, sc.time_unit.NS)
local clock = sc.clock("clock", period)

local memory = simple.Memory("memory", {
  size = MEM_SIZE,
  latency = MEM_LATENCY,
  fifo_size = 1,
})

local cache = simtix.Cache("cache", {
  cache_size_bytes = 256,
  ways = 2,
  block_size_bytes = LINE_SIZE,
  replacement_policy = "fifo",
  write_hit_policy = "WriteBack",
  write_miss_policy = "WriteAllocate",
  mshr_entries = 2,
  mshr_subentries = 4,
  write_buffer_entries = 2,
  pipeline_queue_size = 1,
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

local expected_reads = {}

local line_data = {}

local function seed_line(addr, bytes)
  assert(addr % LINE_SIZE == 0, "seed_line expects a block-aligned address")
  assert(#bytes == LINE_SIZE, "seed_line expects one full cache line")
  line_data[addr] = clone(bytes)
  memory:write_bytes(addr, bytes)
end

local function expected_from_memory(addr, size)
  local base = addr - (addr % LINE_SIZE)
  local bytes = line_data[base]
  assert(bytes, string.format("no seeded line for addr=0x%x", addr))
  return slice(bytes, addr - base, size)
end

local function total_completed() return initiator:completed_count() end

local function wait_until_completed(target_count, max_cycles, label)
  test_utils.wait_until_completed(initiator, target_count, max_cycles, period, label)
end

local function queue_read(addr, size, expected, label)
  initiator:add_payload({ addr = addr, size = size })
  table.insert(expected_reads, {
    data = expected,
    label = label,
  })
end

local function drain_expected_reads(label)
  test_utils.drain_expected_reads(initiator, expected_reads, label)
end

local function run_read_batch(label, requests, opts)
  opts = opts or {}
  print("[READ] " .. label)

  local target_count = total_completed() + #requests
  for _, req in ipairs(requests) do
    queue_read(req.addr, req.size, req.expected, req.label)
  end

  if opts.guard_cycles then
    sc.start(opts.guard_cycles * period)
    assert(
      total_completed() < target_count,
      string.format("%s: burst completed before the backpressure window", label)
    )
  end

  test_utils.assert_unique_expected(expected_reads, label)
  wait_until_completed(target_count, opts.max_cycles or 512, label)
  drain_expected_reads(label)
end

local function read_req(addr, size, label)
  return {
    addr = addr,
    size = size,
    expected = expected_from_memory(addr, size),
    label = label,
  }
end

local LINE_A = 0x0000
local LINE_B = 0x0080
local LINE_C = 0x0100
local LINE_D = 0x0180
local LINE_X = 0x0040
local LINE_M = 0x0200
local LINE_N = 0x0240

seed_line(LINE_A, sequence(LINE_SIZE, 0x10, 3))
seed_line(LINE_B, sequence(LINE_SIZE, 0x40, 5))
seed_line(LINE_C, sequence(LINE_SIZE, 0x70, 7))
seed_line(LINE_D, sequence(LINE_SIZE, 0xa0, 9))
seed_line(LINE_X, sequence(LINE_SIZE, 0xd0, 11))
seed_line(LINE_M, sequence(LINE_SIZE, 0x30, 13))
seed_line(LINE_N, sequence(LINE_SIZE, 0x50, 15))

run_read_batch("MSHR backpressure with more misses than entries", {
  read_req(LINE_A + 4, 8, "primary miss line A"),
  read_req(LINE_B + 8, 16, "independent miss line B"),
  read_req(LINE_C + 0, 16, "third miss waits for an MSHR entry"),
}, {
  guard_cycles = 8,
  max_cycles = 768,
})

local sentinel_b = sequence(LINE_SIZE, 0xe0, 1)
local sentinel_c = sequence(LINE_SIZE, 0xf0, 1)
memory:write_bytes(LINE_B, sentinel_b)
memory:write_bytes(LINE_C, sentinel_c)
line_data[LINE_B] = sentinel_b
line_data[LINE_C] = sentinel_c

run_read_batch("cached hits ignore modified backing memory", {
  {
    addr = LINE_B + 2,
    size = 8,
    expected = slice(sequence(LINE_SIZE, 0x40, 5), 2, 8),
    label = "line B cached hit after backing memory changed",
  },
  {
    addr = LINE_C + 16,
    size = 16,
    expected = slice(sequence(LINE_SIZE, 0x70, 7), 16, 16),
    label = "line C cached hit after backing memory changed",
  },
}, { max_cycles = 64 })

run_read_batch("varied-size read miss on an independent set", {
  read_req(LINE_X + 1, 1, "one-byte read"),
}, { max_cycles = 512 })

run_read_batch("varied-size cached reads on an independent set", {
  read_req(LINE_X + 2, 2, "two-byte read"),
  read_req(LINE_X + 4, 4, "four-byte read"),
  read_req(LINE_X + 8, 8, "eight-byte read"),
  read_req(LINE_X + 16, 16, "sixteen-byte read"),
}, { max_cycles = 64 })

run_read_batch("same-set read allocation evicts the oldest line", {
  read_req(LINE_D + 0, 8, "read line D to evict line B"),
}, { max_cycles = 512 })

run_read_batch("evicted line refetches from backing memory", {
  read_req(LINE_B + 2, 8, "line B refetched after eviction"),
}, { max_cycles = 512 })

run_read_batch("same-line MSHR merge", {
  read_req(LINE_M + 0, 8, "primary miss line M"),
  read_req(LINE_M + 16, 8, "secondary same-line miss merges with M"),
  read_req(LINE_N + 0, 8, "independent miss line N"),
}, {
  guard_cycles = 8,
  max_cycles = 768,
})

print("[PASS] cache_read integration patterns verified at", sc.time_stamp())
