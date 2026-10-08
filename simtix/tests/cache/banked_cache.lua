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
local concurrent = simple.OutstandingInitiator("concurrent")
local mmio_initiator = simple.Initiator("mmio_initiator")
local memory = simple.Memory("memory", { size = MEM_SIZE, latency = 20 })
local cache = BankedCache("cache", {
  cache_size_bytes = 512,
  block_size_bytes = BLOCK,
  num_banks = 4,
  num_froms = 2,
  non_cacheable_regions = { { addr = 0x1000, size = 0x100 } },
  ways = 1,
  write_hit_policy = "WriteBack",
  write_miss_policy = "WriteAllocate",
  replacement_policy = "fifo",
})

initiator.target = cache.port
concurrent.target = cache.port
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

local function read_line(addr, expected, label)
  local target = initiator:completed_count() + 1
  initiator:add_payload({ addr = addr, size = 4 })
  test_utils.wait_until_completed(initiator, target, 1024, period, label)
  test_utils.assert_bytes_equal(initiator:get_read_data(), expected, label)
end

-- Banks have unequal traffic and hit rates; bank 3 remains idle. This catches
-- averaging bank percentages and counting bypass traffic in the denominator.
for _ = 1, 3 do
  read_line(ADDR_SET1, DATA_SET1, "bank 0 hit")
end
read_line(0x40, ZERO, "bank 1 miss")
read_line(0x40, ZERO, "bank 1 hit")
read_line(0x80, ZERO, "bank 2 miss")
read_line(0x1040, ZERO, "bank 1 non-cacheable read")

-- Outstanding reads to a fresh line distinguish primary and secondary misses.
local target = concurrent:completed_count() + 2
concurrent:add_payload({ addr = 0x180, size = 4 })
concurrent:add_payload({ addr = 0x184, size = 4 })
test_utils.wait_until_completed(concurrent, target, 1024, period, "merged reads")
test_utils.assert_bytes_equal(concurrent:get_read_data(), ZERO, "merged read 0")
test_utils.assert_bytes_equal(concurrent:get_read_data(), ZERO, "merged read 1")
sc.start(256 * period)

local function metric(toml, path)
  local section = path:gsub("([^%w])", "%%%1")
  local record = assert(toml:match("%[" .. section .. "%]([^%[]*)"), "missing " .. path)
  local value = assert(record:match("\nval = ([^\n]+)"), "missing value " .. path)
  if value:match("nan$") then return 0 / 0 end
  local number = assert(tonumber(value), "non-numeric value " .. path)
  return number
end

local counters = {
  "total_reads",
  "total_writes",
  "total_atomics",
  "total_cacheable_requests",
  "total_non_cacheable_requests",
  "total_hits",
  "total_misses",
  "primary_misses",
  "secondary_misses",
  "total_evictions",
  "total_requests",
}
local function assert_aggregate()
  local toml = cache.stats:dump_toml()
  local expected = {}
  for _, name in ipairs(counters) do
    local bank_sum = 0
    for bank = 0, 3 do
      bank_sum = bank_sum + metric(toml, "cache.bank" .. bank .. "." .. name)
    end
    assert(metric(toml, "cache." .. name) == bank_sum, name .. ": aggregate differs from banks")
    expected[name] = bank_sum
  end
  local rate = metric(toml, "cache.hit_rate")
  if expected.total_cacheable_requests == 0 then
    assert(rate ~= rate, "empty denominator must retain NaN")
  else
    local expected_rate = expected.total_hits * 100 / expected.total_cacheable_requests
    assert(math.abs(rate - expected_rate) < 1e-10, "hit rate must use summed cacheable requests")
  end
  return expected, toml
end

-- Single-cache counters are the oracle; only the composite's reduction is
-- under test. Check the traffic exercises nonzero bypass and merged counters.
local totals, toml = assert_aggregate()
assert(totals.total_non_cacheable_requests > 0)
assert(totals.secondary_misses > 0)
local mean_rate = 0
for bank = 0, 2 do
  mean_rate = mean_rate + metric(toml, "cache.bank" .. bank .. ".hit_rate") / 3
end
assert(
  math.abs(mean_rate - metric(toml, "cache.hit_rate")) > 1e-10,
  "fixture must distinguish averaging"
)
local idle_rate = metric(toml, "cache.bank3.hit_rate")
assert(idle_rate ~= idle_rate, "idle bank must retain NaN")

cache.stats:reset()
totals = assert_aggregate()
for name, value in pairs(totals) do
  assert(value == 0, name .. ": stale aggregate after reset")
end

read_line(0x40, ZERO, "hit after stats reset")
totals = assert_aggregate()
assert(totals.total_reads > 0, "fixture must produce traffic after reset")

print("Pass!")
