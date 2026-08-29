-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local test_utils = dofile("cache_test_utils.lua")
local bytes_equal = test_utils.bytes_equal

local MEM_SIZE = 65536
local MMIO_BASE = 0
local period = sc.time(1, sc.time_unit.NS)
local MEM_LATENCY = 20 -- Added memory latency
local NUM_TEST_ADDRS = 8 -- Number of addresses to test in/out of range

-- MMIO register offsets
local MMIO_START_OFF = 0x0
local MMIO_ADDR_OFF = 0x8
local MMIO_SIZE_REG_OFF = 0x10
local MMIO_OP_OFF = 0x18

-- MMIO operation and status values
local MMIO_OP_FLUSH = 1

-- New addresses for ranged flush
local FLUSH_RANGE_START = 0x200
-- Flush range size must be large enough for NUM_TEST_ADDRS * cache block size
local FLUSH_RANGE_SIZE = NUM_TEST_ADDRS * 64 -- 512 bytes

-- Procedurally generate addresses and data
local addrs_in_range = {}
local addrs_out_range = {}
local all_addrs = {}
local all_data_A = {} -- First set of test data
local all_data_B = {} -- Second set of test data

for i = 1, NUM_TEST_ADDRS do
  -- Addresses inside the flush range, striding by cache line size
  local addr_in = FLUSH_RANGE_START + (i - 1) * 64
  table.insert(addrs_in_range, addr_in)
  table.insert(all_addrs, addr_in)

  -- Addresses outside the flush range, striding by cache line size
  local addr_out = FLUSH_RANGE_START + FLUSH_RANGE_SIZE + (i - 1) * 64
  table.insert(addrs_out_range, addr_out)
  table.insert(all_addrs, addr_out)

  -- Create unique data for each address
  table.insert(all_data_A, { i, 0xad, 0xbe, 0xef })
  table.insert(all_data_A, { i, 0xbb, 0xcc, 0xdd })
  table.insert(all_data_B, { i, 0x11, 0x22, 0x33 })
  table.insert(all_data_B, { i, 0x55, 0x66, 0x77 })
end

local ZERO = { 0x00, 0x00, 0x00, 0x00 }

local initiator = simple.Initiator("initiator")
local mmio_initiator = simple.Initiator("mmio_initiator")
local memory = simple.Memory("memory", { size = MEM_SIZE, latency = MEM_LATENCY })

local cache = simtix.Cache("cache", {
  cache_size_bytes = 4096,
  ways = 4,
  block_size_bytes = 64,
  replacement_policy = "lru",
  write_hit_policy = "WriteBack",
  write_miss_policy = "WriteAllocate",
})

initiator.target = cache.port
mmio_initiator.target = cache.mmio_port
cache.target = memory.port

local clock = sc.clock("clock", period)
initiator.clock = clock
mmio_initiator.clock = clock
cache.clock = clock
memory.clock = clock

local function do_write(addr, data) initiator:add_payload({ addr = addr, data = data }) end

local function do_read(addr, size) initiator:add_payload({ addr = addr, size = size }) end

local function wait_one_read(tag) return test_utils.wait_read_data(initiator, 2500, period, tag) end

local function mmio_write_u64(offset, value)
  test_utils.mmio_write_u64(
    mmio_initiator,
    MMIO_BASE,
    offset,
    value,
    period,
    2000,
    "MMIO write 0x" .. string.format("%x", offset)
  )
end

local function mmio_read(offset)
  return test_utils.mmio_read_u64(
    mmio_initiator,
    MMIO_BASE,
    offset,
    period,
    2000,
    "MMIO read 0x" .. string.format("%x", offset)
  )
end

local function wait_for_writes(target_count, label)
  test_utils.wait_until_completed(initiator, target_count, 5000, period, label)
end

-- Initialize memory to zero
for _, addr in ipairs(all_addrs) do
  memory:write_bytes(addr, ZERO)
end

lv.info("======================================================")
lv.info("Cache Ranged Flush Test (Expanded)")
lv.info("Policy: WriteHit=WriteBack, WriteMiss=WriteAllocate")
lv.info(
  "Flush Range: 0x"
    .. string.format("%x", FLUSH_RANGE_START)
    .. " - 0x"
    .. string.format("%x", FLUSH_RANGE_START + FLUSH_RANGE_SIZE)
)
lv.info(
  "Testing " .. NUM_TEST_ADDRS .. " addresses in range and " .. NUM_TEST_ADDRS .. " out of range."
)
lv.info("======================================================")

lv.info("Step 1: Write data to cache to create dirty lines")
local write_target = initiator:completed_count() + #all_addrs
for i, addr in ipairs(all_addrs) do
  do_write(addr, all_data_A[i])
end
wait_for_writes(write_target, "initial dirty-line writes")

lv.info("Step 1.5: Verify that data was written correctly to the cache itself")
for i, addr in ipairs(all_addrs) do
  do_read(addr, 4)
end
for i, addr in ipairs(all_addrs) do
  local read_data = wait_one_read("Read back for addr 0x" .. string.format("%x", addr))
  local expected_data = all_data_A[i]
  assert(
    bytes_equal(read_data, expected_data),
    "FAIL: Read-back data at 0x" .. string.format("%x", addr) .. " does not match written data."
  )
end
lv.info("[PASS] All data correctly written to and read back from cache.")

lv.info("Step 2: Verify that backing memory is NOT updated (still zero)")
for i, addr in ipairs(all_addrs) do
  assert(
    bytes_equal(memory:read_bytes(addr, 4), ZERO),
    "FAIL: Mem at 0x" .. string.format("%x", addr) .. " should be zero before flush"
  )
end
lv.info("[PASS] Backing memory is not updated before ranged flush.")

lv.info("Step 3: Set MMIO flush range and trigger flush operation via MMIO")
mmio_write_u64(MMIO_ADDR_OFF, FLUSH_RANGE_START)
mmio_write_u64(MMIO_SIZE_REG_OFF, FLUSH_RANGE_SIZE)
mmio_write_u64(MMIO_OP_OFF, MMIO_OP_FLUSH)
mmio_write_u64(MMIO_START_OFF, 1)

lv.info("Step 4: Poll MMIO start bit ")
local start_bit = mmio_read(MMIO_START_OFF)
assert(start_bit == 1, "FAIL: MMIO start bit should be 1")

test_utils.wait_mmio_idle(mmio_initiator, MMIO_BASE, MMIO_START_OFF, period, 2000, "ranged flush")
lv.info("[PASS] Ranged flush operation completed.")

lv.info(
  "Step 5: Verify that backing memory IS updated for IN-RANGE addresses and NOT for OUT-OF-RANGE addresses"
)
-- Verify in-range addresses are flushed
for i, addr in ipairs(addrs_in_range) do
  local data_idx = (i - 1) * 2 + 1
  local expected_data = all_data_A[data_idx]
  local actual_data = memory:read_bytes(addr, 4)
  assert(
    bytes_equal(actual_data, expected_data),
    "FAIL: Mem at IN_RANGE 0x"
      .. string.format("%x", addr)
      .. " should be updated after ranged flush"
  )
  lv.info(string.format("[OK] IN_RANGE addr 0x%x flushed correctly.", addr))
end
-- Verify out-of-range addresses are NOT flushed
for i, addr in ipairs(addrs_out_range) do
  local actual_data = memory:read_bytes(addr, 4)
  assert(
    bytes_equal(actual_data, ZERO),
    "FAIL: Mem at OUT_RANGE 0x"
      .. string.format("%x", addr)
      .. " should remain ZERO after ranged flush"
  )
  lv.info(string.format("[OK] OUT_RANGE addr 0x%x correctly not flushed.", addr))
end
lv.info("[PASS] Backing memory correctly updated for ranged flush.")

lv.info("Step 6: Write new data to all addresses to create dirty lines")
write_target = initiator:completed_count() + #all_addrs
for i, addr in ipairs(all_addrs) do
  do_write(addr, all_data_B[i])
end
wait_for_writes(write_target, "second dirty-line writes")

lv.info("Step 7: Verify backing memory not updated before full flush")
-- Verify in-range addresses have old flushed data
for i, addr in ipairs(addrs_in_range) do
  local data_idx = (i - 1) * 2 + 1
  local expected_data = all_data_A[data_idx]
  local actual_data = memory:read_bytes(addr, 4)
  assert(
    bytes_equal(actual_data, expected_data),
    "FAIL: Mem at IN_RANGE 0x"
      .. string.format("%x", addr)
      .. " should still have old data before full flush"
  )
  lv.info(string.format("[OK] IN_RANGE addr 0x%x still has old data.", addr))
end
-- Verify out-of-range addresses are still zero
for i, addr in ipairs(addrs_out_range) do
  local actual_data = memory:read_bytes(addr, 4)
  assert(
    bytes_equal(actual_data, ZERO),
    "FAIL: Mem at OUT_RANGE 0x"
      .. string.format("%x", addr)
      .. " should still be ZERO before full flush"
  )
  lv.info(string.format("[OK] OUT_RANGE addr 0x%x is still zero.", addr))
end
lv.info("[PASS] Backing memory unchanged before full flush.")

lv.info("Step 8: Trigger full flush via MMIO (size=0)")
mmio_write_u64(MMIO_ADDR_OFF, 0)
mmio_write_u64(MMIO_SIZE_REG_OFF, 0)
mmio_write_u64(MMIO_OP_OFF, MMIO_OP_FLUSH)
mmio_write_u64(MMIO_START_OFF, 1)

lv.info("Step 9: Poll MMIO start bit until full flush is done")
start_bit = mmio_read(MMIO_START_OFF)
assert(start_bit == 1, "FAIL: MMIO start bit should be 1 after starting full flush")

test_utils.wait_mmio_idle(mmio_initiator, MMIO_BASE, MMIO_START_OFF, period, 4000, "full flush")
lv.info("[PASS] Full flush operation completed.")
test_utils.wait_until(function()
  for i, addr in ipairs(all_addrs) do
    if not bytes_equal(memory:read_bytes(addr, 4), all_data_B[i]) then return false end
  end
  return true
end, 5000, period, "full flush write-backs reach memory")

lv.info("Step 10: Verify backing memory updated for all addresses")
for i, addr in ipairs(all_addrs) do
  local expected_data = all_data_B[i]
  local actual_data = memory:read_bytes(addr, 4)
  assert(
    bytes_equal(actual_data, expected_data),
    "FAIL: Mem at 0x"
      .. string.format("%x", addr)
      .. " should be updated to new data after full flush"
  )
  lv.info(string.format("[OK] Addr 0x%x correctly updated after full flush.", addr))
end
lv.info("[PASS] Backing memory correctly updated for full flush.")

lv.info("\n[SUCCESS] Cache flush tests passed!")
