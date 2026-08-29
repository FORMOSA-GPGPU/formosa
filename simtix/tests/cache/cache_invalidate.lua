-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local test_utils = dofile("cache_test_utils.lua")
local bytes_equal = test_utils.bytes_equal

local MEM_SIZE = 65536
local MMIO_BASE = 0
local period = sc.time(1, sc.time_unit.NS)

-- MMIO register offsets
local MMIO_START_OFF = 0x0
local MMIO_ADDR_OFF = 0x8
local MMIO_SIZE_REG_OFF = 0x10
local MMIO_OP_OFF = 0x18

-- MMIO operation values
local MMIO_OP_INVALIDATE = 2

local NUM_TEST_ADDRS = 8

-- Addresses for range and data
local FLUSH_RANGE_START = 0x200
local FLUSH_RANGE_SIZE = NUM_TEST_ADDRS * 64 -- 512 bytes

-- Procedurally generate addresses and data
local addrs_in_range = {}
local addrs_out_range = {}
local all_addrs = {}
local all_data_A = {}
local all_data_B = {}

for i = 1, NUM_TEST_ADDRS do
  local addr_in = FLUSH_RANGE_START + (i - 1) * 64
  table.insert(addrs_in_range, addr_in)
  table.insert(all_addrs, addr_in)

  local addr_out = FLUSH_RANGE_START + FLUSH_RANGE_SIZE + (i - 1) * 64
  table.insert(addrs_out_range, addr_out)
  table.insert(all_addrs, addr_out)

  table.insert(all_data_A, { i, 0xde, 0xad, 0xbe })
  table.insert(all_data_A, { i, 0xaa, 0xbb, 0xcc })
  table.insert(all_data_B, { i, 0x11, 0x22, 0x33 })
  table.insert(all_data_B, { i, 0x55, 0x66, 0x77 })
end

local ZERO = { 0x00, 0x00, 0x00, 0x00 }

local initiator = simple.Initiator("initiator")
local mmio_initiator = simple.Initiator("mmio_initiator")
local memory = simple.Memory("memory", { size = MEM_SIZE })

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

print("======================================================")
print("Cache Ranged Invalidate Test")
print("Policy: WriteHit=WriteBack, WriteMiss=WriteAllocate")
print(
  "Invalidate Range: 0x"
    .. string.format("%x", FLUSH_RANGE_START)
    .. " - 0x"
    .. string.format("%x", FLUSH_RANGE_START + FLUSH_RANGE_SIZE)
)
print(
  "Testing " .. NUM_TEST_ADDRS .. " addresses in range and " .. NUM_TEST_ADDRS .. " out of range."
)
print("======================================================")

print("Step 1: Write data to cache to create dirty lines (some in range, some out)")
local write_target = initiator:completed_count() + #all_addrs
for i, addr in ipairs(all_addrs) do
  do_write(addr, all_data_A[i])
end
wait_for_writes(write_target, "initial dirty-line writes")

print("Step 1.5: Verify that data was written correctly to the cache itself")
for _, addr in ipairs(all_addrs) do
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
print("[PASS] All data correctly written to and read back from cache.")

print("Step 2: Verify that backing memory is NOT updated (still zero)")
for _, addr in ipairs(all_addrs) do
  assert(
    bytes_equal(memory:read_bytes(addr, 4), ZERO),
    "FAIL: Mem at 0x" .. string.format("%x", addr) .. " should be zero before invalidate"
  )
end
print("[PASS] Backing memory is not updated before invalidate.")

print("Step 3: Set MMIO invalidate range and trigger invalidate operation via MMIO")
mmio_write_u64(MMIO_ADDR_OFF, FLUSH_RANGE_START)
mmio_write_u64(MMIO_SIZE_REG_OFF, FLUSH_RANGE_SIZE)
mmio_write_u64(MMIO_OP_OFF, MMIO_OP_INVALIDATE)
mmio_write_u64(MMIO_START_OFF, 1)

print("Step 4: Poll MMIO start bit ")
local start_bit = mmio_read(MMIO_START_OFF)
assert(start_bit == 1, "FAIL: MMIO start bit should be 1")

test_utils.wait_mmio_idle(
  mmio_initiator,
  MMIO_BASE,
  MMIO_START_OFF,
  period,
  2000,
  "ranged invalidate"
)
print("[PASS] Invalidate operation completed.")

print("Step 5: Verify backing memory is still zero (invalidate does not write back)")
for _, addr in ipairs(all_addrs) do
  assert(
    bytes_equal(memory:read_bytes(addr, 4), ZERO),
    "FAIL: Mem at 0x" .. string.format("%x", addr) .. " should remain ZERO after invalidate"
  )
end
print("[PASS] Backing memory unchanged after invalidate.")

print("Step 6: Read through cache to confirm in-range lines invalidated")
for _, addr in ipairs(addrs_in_range) do
  do_read(addr, 4)
end
for _, addr in ipairs(addrs_in_range) do
  assert(
    bytes_equal(wait_one_read("IN_RANGE"), ZERO),
    "FAIL: IN_RANGE 0x" .. string.format("%x", addr) .. " should read ZERO after invalidate"
  )
end

print("Step 7: Read through cache to confirm out-of-range lines remain")
for _, addr in ipairs(addrs_out_range) do
  do_read(addr, 4)
end
for i, addr in ipairs(addrs_out_range) do
  local data_idx = (i - 1) * 2 + 1
  local expected_data = all_data_A[data_idx + 1]
  assert(
    bytes_equal(wait_one_read("OUT_RANGE"), expected_data),
    "FAIL: OUT_RANGE 0x" .. string.format("%x", addr) .. " should still be old data"
  )
end

print("Step 8: Write new data to all addresses to create dirty lines")
write_target = initiator:completed_count() + #all_addrs
for i, addr in ipairs(all_addrs) do
  do_write(addr, all_data_B[i])
end
wait_for_writes(write_target, "second dirty-line writes")

print("Step 9: Verify backing memory still zero before full invalidate")
for _, addr in ipairs(all_addrs) do
  assert(
    bytes_equal(memory:read_bytes(addr, 4), ZERO),
    "FAIL: Mem at 0x" .. string.format("%x", addr) .. " should still be ZERO before full invalidate"
  )
end
print("[PASS] Backing memory unchanged before full invalidate.")

print("Step 10: Trigger full invalidate via MMIO (size=0)")
mmio_write_u64(MMIO_ADDR_OFF, 0)
mmio_write_u64(MMIO_SIZE_REG_OFF, 0)
mmio_write_u64(MMIO_OP_OFF, MMIO_OP_INVALIDATE)
mmio_write_u64(MMIO_START_OFF, 1)

print("Step 11: Poll MMIO start bit until full invalidate is done")
start_bit = mmio_read(MMIO_START_OFF)
assert(start_bit == 1, "FAIL: MMIO start bit should be 1 after starting full invalidate")

test_utils.wait_mmio_idle(
  mmio_initiator,
  MMIO_BASE,
  MMIO_START_OFF,
  period,
  4000,
  "full invalidate"
)
print("[PASS] Full invalidate operation completed.")

print("Step 12: Verify backing memory still zero (full invalidate does not write back)")
for _, addr in ipairs(all_addrs) do
  assert(
    bytes_equal(memory:read_bytes(addr, 4), ZERO),
    "FAIL: Mem at 0x" .. string.format("%x", addr) .. " should remain ZERO after full invalidate"
  )
end
print("[PASS] Backing memory unchanged after full invalidate.")

print("Step 13: Read through cache to confirm all lines invalidated")
for _, addr in ipairs(all_addrs) do
  do_read(addr, 4)
end
for _, addr in ipairs(all_addrs) do
  assert(
    bytes_equal(wait_one_read("FULL"), ZERO),
    "FAIL: Addr 0x" .. string.format("%x", addr) .. " should read ZERO after full invalidate"
  )
end

print("\n[SUCCESS] Cache invalidate tests passed!")
