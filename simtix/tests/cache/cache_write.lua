-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local test_utils = dofile("cache_test_utils.lua")
local fill = test_utils.fill
local sequence = test_utils.sequence
local slice = test_utils.slice
local with_patch = test_utils.with_patch
local assert_bytes_equal = test_utils.assert_bytes_equal

local function contains(values, candidate)
  for _, value in ipairs(values) do
    if value == candidate then return true end
  end
  return false
end

local function parse_args(argv)
  local args = {
    replacement_policy = "fifo",
    write_hit_policy = "WriteBack",
    write_miss_policy = "WriteNoAllocate",
  }

  local i = 1
  while i <= #argv do
    local option = argv[i]
    local value = argv[i + 1]
    assert(value ~= nil, "missing value for " .. tostring(option))

    if option == "--replacement_policy" then
      args.replacement_policy = value
    elseif option == "--write_hit_policy" then
      args.write_hit_policy = value
    elseif option == "--write_miss_policy" then
      args.write_miss_policy = value
    else
      error("unknown option " .. tostring(option))
    end

    i = i + 2
  end

  assert(
    contains({ "fifo", "lru", "random" }, args.replacement_policy),
    "invalid replacement_policy"
  )
  assert(
    contains({ "WriteBack", "WriteThrough" }, args.write_hit_policy),
    "invalid write_hit_policy"
  )
  assert(
    contains({ "WriteAllocate", "WriteNoAllocate" }, args.write_miss_policy),
    "invalid write_miss_policy"
  )

  return args
end

local args = parse_args({ ... })

local MEM_SIZE = 65536
local LINE_SIZE = 16
local MEM_LATENCY = 28

local REPLACEMENT_POLICY = args.replacement_policy
local WRITE_HIT_POLICY = args.write_hit_policy
local WRITE_MISS_POLICY = args.write_miss_policy

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
  replacement_policy = REPLACEMENT_POLICY,
  write_hit_policy = WRITE_HIT_POLICY,
  write_miss_policy = WRITE_MISS_POLICY,
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

local function assert_memory(addr, expected, label)
  assert_bytes_equal(memory:read_bytes(addr, #expected), expected, label)
end

local function total_completed() return initiator:completed_count() end

local function wait_until_completed(target_count, max_cycles, label)
  test_utils.wait_until_completed(initiator, target_count, max_cycles, period, label)
end

local function queue_read(req)
  initiator:add_payload({ addr = req.addr, size = req.size })
  table.insert(expected_reads, {
    data = req.expected,
    label = req.label,
  })
end

local function queue_write(req) initiator:add_payload({ addr = req.addr, data = req.data }) end

local function drain_expected_reads(label)
  test_utils.drain_expected_reads(initiator, expected_reads, label)
end

local function wait_memory(addr, expected, max_cycles, label)
  test_utils.wait_until(
    function() return test_utils.bytes_equal(memory:read_bytes(addr, #expected), expected) end,
    max_cycles,
    period,
    label
  )
end

local function run_writes(label, writes, opts)
  opts = opts or {}
  print("[WRITE] " .. label)

  local target_count = total_completed() + #writes
  for _, req in ipairs(writes) do
    queue_write(req)
  end

  if opts.guard_cycles then
    sc.start(opts.guard_cycles * period)
    assert(
      total_completed() < target_count,
      string.format("%s: write burst completed before the backpressure window", label)
    )
  end

  wait_until_completed(target_count, opts.max_cycles or 768, label)
end

local function run_reads(label, reads, opts)
  opts = opts or {}
  print("[READ] " .. label)

  local target_count = total_completed() + #reads
  for _, req in ipairs(reads) do
    queue_read(req)
  end

  test_utils.assert_unique_expected(expected_reads, label)
  wait_until_completed(target_count, opts.max_cycles or 768, label)
  drain_expected_reads(label)
end

local function read_req(addr, size, expected, label)
  return {
    addr = addr,
    size = size,
    expected = expected,
    label = label,
  }
end

local function write_req(addr, data, label)
  return {
    addr = addr,
    data = data,
    label = label,
  }
end

local ZERO_LINE = fill(LINE_SIZE, 0x00)

local A = 0x0000
local B = 0x0010
local C = 0x0020
local D = 0x0030

local DATA_A = { 0xde, 0xad, 0xbe, 0xef }
local DATA_B = { 0x11, 0x22, 0x33, 0x44 }
local DATA_C = { 0x55, 0x66, 0x77, 0x88 }
local DATA_D = { 0x99, 0xaa, 0xbb, 0xcc }

for _, addr in ipairs({ A, B, C, D }) do
  memory:write_bytes(addr, ZERO_LINE)
end

print("======================================================")
print(
  "Policy: Replacement="
    .. REPLACEMENT_POLICY
    .. " WriteHit="
    .. WRITE_HIT_POLICY
    .. " WriteMiss="
    .. WRITE_MISS_POLICY
)
print("======================================================")

local miss_burst = {
  write_req(A + 0, DATA_A, "write miss A"),
  write_req(B + 4, DATA_B, "write miss B"),
  write_req(C + 8, DATA_C, "write miss C"),
}

run_writes("write-miss burst with small MSHR/write-buffer capacity", miss_burst, {
  guard_cycles = WRITE_MISS_POLICY == "WriteAllocate" and 8 or nil,
  max_cycles = 1200,
})

if WRITE_MISS_POLICY == "WriteNoAllocate" then
  for _, req in ipairs(miss_burst) do
    wait_memory(
      req.addr,
      req.data,
      1200,
      req.label .. " updates backing memory under WriteNoAllocate"
    )
  end
end

for index, req in ipairs(miss_burst) do
  req.sentinel = fill(#req.data, 0x50 + index)
  memory:write_bytes(req.addr, req.sentinel)
end

local miss_policy_reads = {}
for _, req in ipairs(miss_burst) do
  local expected = WRITE_MISS_POLICY == "WriteAllocate" and req.data or req.sentinel
  table.insert(
    miss_policy_reads,
    read_req(req.addr, #req.data, expected, req.label .. " allocation policy check")
  )
end

run_reads("verify write-miss allocation policy", miss_policy_reads, {
  max_cycles = 1200,
})

local H0 = 0x0100
local H1 = 0x0120
local H2 = 0x0140

local H0_BASE = sequence(LINE_SIZE, 0x20, 3)
local H1_BASE = sequence(LINE_SIZE, 0x60, 5)
local H2_BASE = sequence(LINE_SIZE, 0xa0, 7)
local PATCH = { 0xc1, 0xc2, 0xc3, 0xc4 }
local H0_PATCHED = with_patch(H0_BASE, 4, PATCH)

memory:write_bytes(H0, H0_BASE)
memory:write_bytes(H1, H1_BASE)
memory:write_bytes(H2, H2_BASE)

run_reads("read-allocate lines used by the write-hit tests", {
  read_req(H0, LINE_SIZE, H0_BASE, "fill H0"),
  read_req(H1, LINE_SIZE, H1_BASE, "fill H1"),
}, { max_cycles = 768 })

run_writes("partial write hit", {
  write_req(H0 + 4, PATCH, "partial write hit H0"),
}, {
  max_cycles = 256,
})

run_reads("partial write hit is visible through the cache", {
  read_req(H0, LINE_SIZE, H0_PATCHED, "read patched H0"),
}, { max_cycles = 256 })

if WRITE_HIT_POLICY == "WriteThrough" then
  wait_memory(H0 + 4, PATCH, 512, "WriteThrough hit updates backing memory")
else
  assert_memory(H0 + 4, slice(H0_BASE, 4, #PATCH), "WriteBack hit stays dirty in cache")
end

run_reads("make H0 the eviction victim under FIFO and LRU", {
  read_req(H1, 4, slice(H1_BASE, 0, 4), "touch H1 before eviction"),
}, { max_cycles = 256 })

run_reads("force same-set eviction with H2", {
  read_req(H2, LINE_SIZE, H2_BASE, "fill H2 and evict H0"),
}, { max_cycles = 768 })

wait_memory(H0 + 4, PATCH, 768, "dirty/write-through data reaches memory after eviction")

local SENT_LINE = fill(LINE_SIZE, 0x6b)
memory:write_bytes(H0, SENT_LINE)

run_reads("evicted dirty line refetches from backing memory", {
  read_req(H0 + 4, #PATCH, slice(SENT_LINE, 4, #PATCH), "H0 refetch after eviction"),
}, { max_cycles = 768 })

print("[PASS] cache_write integration patterns verified at", sc.time_stamp())
