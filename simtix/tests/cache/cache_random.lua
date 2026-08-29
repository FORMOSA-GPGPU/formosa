-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local bit = require("bit")
local test_utils = dofile("cache_test_utils.lua")

local function contains(values, candidate)
  for _, value in ipairs(values) do
    if value == candidate then return true end
  end
  return false
end

local function parse_args(argv)
  local args = {
    replacement_policy = "random",
    write_hit_policy = "WriteBack",
    write_miss_policy = "WriteAllocate",
    seed = 12345,
    operations = 600,
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
    elseif option == "--seed" then
      args.seed = tonumber(value)
    elseif option == "--operations" then
      args.operations = tonumber(value)
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
  assert(args.seed ~= nil and args.seed >= 0 and args.seed == math.floor(args.seed), "invalid seed")
  assert(
    args.operations ~= nil
      and args.operations > 0
      and args.operations == math.floor(args.operations),
    "invalid operations"
  )

  return args
end

local args = parse_args({ ... })

local MEM_SIZE = 16 * 1024
local WORKING_SIZE = 4 * 1024
local LINE_SIZE = 64
local NUM_SETS = 4
local CACHE_SIZE = NUM_SETS * 2 * LINE_SIZE
local MEM_LATENCY = 20

local MAX_REQUEST_CYCLES = 2048
local MAX_MMIO_CYCLES = 4096
local TRACE_KEEP = 32
local WRITE_RATIO = 0.55
local RAW_RATIO = 0.35
local SIZES = { 1, 2, 4, 8, 16, 32, 64 }

local MMIO_START_OFF = 0x0
local MMIO_ADDR_OFF = 0x8
local MMIO_SIZE_OFF = 0x10
local MMIO_OP_OFF = 0x18
local MMIO_OP_FLUSH = 1

local period = sc.time(1, sc.time_unit.NS)
local clock = sc.clock("clock", period)

local memory = simple.Memory("memory", {
  size = MEM_SIZE,
  latency = MEM_LATENCY,
  fifo_size = 1,
})

local cache = simtix.Cache("cache", {
  cache_size_bytes = CACHE_SIZE,
  ways = 2,
  block_size_bytes = LINE_SIZE,
  replacement_policy = args.replacement_policy,
  write_hit_policy = args.write_hit_policy,
  write_miss_policy = args.write_miss_policy,
  random_seed = args.seed,
  mshr_entries = 2,
  mshr_subentries = 4,
  write_buffer_entries = 2,
  victim_buffer_entries = 2,
  pipeline_queue_size = 1,
})

local initiator = simple.OutstandingInitiator("initiator")
local mmio_initiator = simple.Initiator("mmio_initiator")

initiator.target = cache.port
mmio_initiator.target = cache.mmio_port
cache.target = memory.port
mmio_initiator.clock = clock
cache.clock = clock
memory.clock = clock

local trace = {}
local oracle = {}
local request_count = 0
local read_count = 0
local write_count = 0
local current_step = "initialization"

local function trace_push(entry)
  trace[#trace + 1] = entry
  if #trace > TRACE_KEEP then table.remove(trace, 1) end
end

local function trace_dump()
  if #trace == 0 then return "  <empty>" end
  return "  " .. table.concat(trace, "\n  ")
end

local function bytes_hex(bytes)
  if bytes == nil then return "<nil>" end
  local out = {}
  for i = 1, #bytes do
    out[i] = string.format("%02x", bytes[i])
  end
  return table.concat(out)
end

local function fail(label, detail)
  error(
    string.format(
      "%s\nseed=%d step=%s requests=%d policy=%s/%s/%s\n%s\nrecent trace:\n%s",
      label,
      args.seed,
      current_step,
      request_count,
      args.replacement_policy,
      args.write_hit_policy,
      args.write_miss_policy,
      detail or "",
      trace_dump()
    )
  )
end

local function assert_bytes_equal(actual, expected, label)
  if actual == nil or #actual ~= #expected then
    fail(
      label,
      string.format("size mismatch: actual=%s expected=%d", actual and #actual or "nil", #expected)
    )
  end

  for i = 1, #expected do
    if actual[i] ~= expected[i] then
      fail(
        label,
        string.format(
          "byte %d mismatch: actual=0x%02x expected=0x%02x\nactual=%s\nexpected=%s",
          i,
          actual[i],
          expected[i],
          bytes_hex(actual),
          bytes_hex(expected)
        )
      )
    end
  end
end

local function oracle_read(addr, size)
  local data = {}
  for i = 1, size do
    data[i] = oracle[addr + i]
  end
  return data
end

local function oracle_write(addr, data)
  for i = 1, #data do
    oracle[addr + i] = data[i]
  end
end

local function wait_until_completed(source, target_count, max_cycles, label)
  for _ = 1, max_cycles do
    if source:completed_count() >= target_count then return end
    sc.start(period)
  end

  fail(
    label,
    string.format(
      "timeout: completed=%d expected=%d after %d cycles",
      source:completed_count(),
      target_count,
      max_cycles
    )
  )
end

local function checked_read(addr, size, label)
  local expected = oracle_read(addr, size)
  local target_count = initiator:completed_count() + 1
  request_count = request_count + 1
  read_count = read_count + 1
  trace_push(string.format("R req=%d addr=0x%04x size=%d %s", request_count, addr, size, label))

  initiator:add_payload({ addr = addr, size = size })
  wait_until_completed(initiator, target_count, MAX_REQUEST_CYCLES, label)

  local actual = initiator:get_read_data()
  assert_bytes_equal(actual, expected, label)
  if initiator:get_read_data() ~= nil then fail(label, "unexpected extra read response") end
end

local function checked_write(addr, data, label)
  local target_count = initiator:completed_count() + 1
  request_count = request_count + 1
  write_count = write_count + 1
  trace_push(
    string.format(
      "W req=%d addr=0x%04x size=%d data=%s %s",
      request_count,
      addr,
      #data,
      bytes_hex(data),
      label
    )
  )

  initiator:add_payload({ addr = addr, data = data })
  wait_until_completed(initiator, target_count, MAX_REQUEST_CYCLES, label)
  oracle_write(addr, data)
end

local function random_data(size)
  local data = {}
  for i = 1, size do
    data[i] = math.random(0, 0xff)
  end
  return data
end

local function random_line()
  local choice = math.random()
  if choice < 0.40 then
    -- Revisit a cache-sized hot set to create hits and overlapping updates.
    return math.random(0, CACHE_SIZE / LINE_SIZE - 1)
  elseif choice < 0.80 then
    -- Pick many tags mapping to one set to force clean and dirty replacement.
    local set = math.random(0, NUM_SETS - 1)
    local tags_in_working_set = WORKING_SIZE / LINE_SIZE / NUM_SETS
    return set + math.random(0, tags_in_working_set - 1) * NUM_SETS
  end

  return math.random(0, WORKING_SIZE / LINE_SIZE - 1)
end

local function random_access()
  local size = SIZES[math.random(1, #SIZES)]
  local line = random_line()
  local max_offset = LINE_SIZE - size
  local offset = 0

  if max_offset > 0 then
    if math.random() < 0.65 then
      local edges = { 0, 1, max_offset, math.max(0, max_offset - 1), math.floor(max_offset / 2) }
      offset = edges[math.random(1, #edges)]
    else
      offset = math.random(0, max_offset)
    end
  end

  return line * LINE_SIZE + offset, size
end

local function mmio_write(offset, value, label)
  local target_count = mmio_initiator:completed_count() + 1
  mmio_initiator:add_payload({ addr = offset, data = test_utils.u64_bytes(value) })
  wait_until_completed(mmio_initiator, target_count, MAX_MMIO_CYCLES, label)
end

local function mmio_read(offset, label)
  local target_count = mmio_initiator:completed_count() + 1
  mmio_initiator:add_payload({ addr = offset, size = 8 })
  wait_until_completed(mmio_initiator, target_count, MAX_MMIO_CYCLES, label)

  local data = mmio_initiator:get_read_data()
  if data == nil then fail(label, "MMIO read completed without data") end

  return test_utils.u64_from_bytes(data)
end

local function flush_all()
  current_step = "full flush"
  trace_push("MMIO full flush")
  mmio_write(MMIO_ADDR_OFF, 0, "configure full-flush address")
  mmio_write(MMIO_SIZE_OFF, 0, "configure full-flush size")
  mmio_write(MMIO_OP_OFF, MMIO_OP_FLUSH, "configure full-flush operation")
  mmio_write(MMIO_START_OFF, 1, "start full flush")

  for _ = 1, MAX_MMIO_CYCLES do
    if mmio_read(MMIO_START_OFF, "poll full flush") == 0 then return end
    sc.start(period)
  end
  fail("full flush", "timeout waiting for MMIO start bit to clear")
end

-- Seed both the backing memory and the independent byte-accurate oracle with a
-- non-zero pattern. This catches lost writes as well as reads that incorrectly
-- return zero-filled cache data.
for line_addr = 0, MEM_SIZE - LINE_SIZE, LINE_SIZE do
  local data = {}
  for i = 1, LINE_SIZE do
    local addr = line_addr + i - 1
    data[i] = bit.band(addr * 17 + math.floor(addr / LINE_SIZE) * 29 + 0x5a, 0xff)
    oracle[addr + 1] = data[i]
  end
  memory:write_bytes(line_addr, data)
end

math.randomseed(args.seed)

print(
  string.format(
    "[RANDOM] seed=%d operations=%d policy=%s/%s/%s",
    args.seed,
    args.operations,
    args.replacement_policy,
    args.write_hit_policy,
    args.write_miss_policy
  )
)

-- A short deterministic prefix guarantees coverage of partial-line writes,
-- accesses ending at a line boundary, overlapping writes, and same-set dirty
-- replacement before the generated stream begins.
checked_read(0x01, 1, "directed unaligned cold read")
checked_write(
  LINE_SIZE - 8,
  { 0xde, 0xad, 0xbe, 0xef, 0x11, 0x22, 0x33, 0x44 },
  "directed line-end write"
)
checked_write(LINE_SIZE - 5, { 0xa1, 0xb2, 0xc3 }, "directed overlapping write")
checked_read(LINE_SIZE - 8, 8, "directed overlapping read-after-write")
for tag = 0, 4 do
  local addr = (tag * NUM_SETS + 1) * LINE_SIZE + 7
  checked_write(addr, { tag, 0x80 + tag, 0xf0 - tag }, "directed same-set conflict")
end

for operation = 1, args.operations do
  current_step = "random operation " .. operation
  local addr, size = random_access()
  if math.random() < WRITE_RATIO then
    checked_write(addr, random_data(size), "random operation " .. operation)
    if math.random() < RAW_RATIO then
      checked_read(addr, size, "random read-after-write " .. operation)
    end
  else
    checked_read(addr, size, "random operation " .. operation)
  end
end

-- Sweep a working set much larger than the cache. Besides checking every byte,
-- this creates deterministic replacement pressure after the random stream.
for addr = 0, WORKING_SIZE - LINE_SIZE, LINE_SIZE do
  current_step = string.format("final sweep at 0x%04x", addr)
  checked_read(addr, LINE_SIZE, "final working-set sweep")
end

-- Flush remaining dirty lines and compare the backing memory against the same
-- oracle. This detects write-back corruption that cache-side reads can hide.
flush_all()
sc.start((MEM_LATENCY * CACHE_SIZE / LINE_SIZE + 64) * period)

for addr = 0, WORKING_SIZE - LINE_SIZE, LINE_SIZE do
  current_step = string.format("backing-memory verification at 0x%04x", addr)
  assert_bytes_equal(
    memory:read_bytes(addr, LINE_SIZE),
    oracle_read(addr, LINE_SIZE),
    string.format("backing-memory verification at 0x%04x", addr)
  )
end

print(
  string.format(
    "[PASS] seed=%d generated_operations=%d requests=%d reads=%d writes=%d",
    args.seed,
    args.operations,
    request_count,
    read_count,
    write_count
  )
)
