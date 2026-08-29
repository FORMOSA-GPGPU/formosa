-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local MEM_SIZE = 65536
local MEM_LATENCY = 50
local period = sc.time(1, sc.time_unit.NS)
local clock = sc.clock("clock", period)

----------------------------------------------------------------------
-- 1. PC-based Stride Prefetcher Logic
----------------------------------------------------------------------

-- Use a table to track the behavior of each Program Counter (PC/IP)
local pc_history = {}

-- Stride prefetcher function
local function stride_prefetcher(addr, ip, line_size)
  if not ip or ip == 0 then return nil end -- Do not prefetch if IP is invalid

  local history = pc_history[ip]
  local prefetch_addresses = {}

  if history then
    -- Calculate the stride based on the last access from the same IP
    local stride = addr - history.last_addr

    -- If the stride is stable and non-zero, we predict the next access
    if stride == history.last_stride and stride ~= 0 then
      local prefetch_addr = addr + stride
      print(
        string.format(
          "[Prefetcher] PC=0x%x, Addr=0x%x, Stride=%d -> Prefetching Addr=0x%x",
          ip,
          addr,
          stride,
          prefetch_addr
        )
      )
      prefetch_addresses[#prefetch_addresses + 1] = prefetch_addr
    end

    -- Update history for the current IP
    history.last_stride = stride
    history.last_addr = addr
  else
    -- First time seeing this IP, create a new history entry
    pc_history[ip] = {
      last_addr = addr,
      last_stride = 0, -- Initial stride is 0
    }
  end

  return prefetch_addresses
end

----------------------------------------------------------------------
-- 2. Testbench Setup
----------------------------------------------------------------------

-- Cache with PC-based prefetcher
local cache_pc_pf = simtix.Cache("cache_pc_pf", {
  size_bytes = 4096,
  ways = 4,
  block_size_bytes = 64,
  replacement_policy = "lru",
  write_hit_policy = "WriteBack",
  write_miss_policy = "WriteAllocate",
  prefetch_fn = stride_prefetcher,
  prefetch_max = 4,
})

-- Cache without prefetching for baseline comparison
local cache_no_pf = simtix.Cache("cache_no_pf", {
  size_bytes = 4096,
  ways = 4,
  block_size_bytes = 64,
  replacement_policy = "lru",
  prefetch_max = 0,
})

local initiator_pc_pf = simple.Initiator("initiator_pc_pf")

local initiator_no_pf = simple.Initiator("initiator_no_pf")

local memory_pc_pf = simple.Memory("memory_pc_pf", { size = MEM_SIZE, latency = MEM_LATENCY })

local memory_no_pf = simple.Memory("memory_no_pf", { size = MEM_SIZE, latency = MEM_LATENCY })

-- Wire up components for PC Prefetch path

initiator_pc_pf.target = cache_pc_pf.port

cache_pc_pf.target = memory_pc_pf.port

initiator_pc_pf.clock = clock

cache_pc_pf.clock = clock

memory_pc_pf.clock = clock

-- Wire up components for Baseline path

initiator_no_pf.target = cache_no_pf.port

cache_no_pf.target = memory_no_pf.port

initiator_no_pf.clock = clock

cache_no_pf.clock = clock

memory_no_pf.clock = clock

-- The 'mmio_port' must be bound, even if unused. We create dummy initiators

-- to terminate these ports.

local mmio_initiator_pc_pf = simple.Initiator("mmio_initiator_pc_pf")

local mmio_initiator_no_pf = simple.Initiator("mmio_initiator_no_pf")

mmio_initiator_pc_pf.target = cache_pc_pf.mmio_port

mmio_initiator_no_pf.target = cache_no_pf.mmio_port

mmio_initiator_pc_pf.clock = clock

mmio_initiator_no_pf.clock = clock

----------------------------------------------------------------------

-- 3. Test Scenario & Execution

----------------------------------------------------------------------

local LINE_SIZE = 64

local STRIDE = LINE_SIZE * 2 -- Access with a stride of 128 bytes

local BASE_ADDR = 0x1000

local LOOP_PC = 0xBEEF -- A simulated PC for our looping instruction

local NUM_ACCESSES = 100

local MAX_CYCLES = MEM_LATENCY * 2

-- Fill both memories with the same data

for i = 0, NUM_ACCESSES do
  local data_to_write = { 0xDE, 0xAD, 0xBE, 0xEF }

  memory_pc_pf:write_bytes(BASE_ADDR + i * STRIDE, data_to_write)

  memory_no_pf:write_bytes(BASE_ADDR + i * STRIDE, data_to_write)
end

-- Helper to read from memory and measure latency
local function read_and_measure_cycles(initiator, addr, ip, size, max_cycles, tag)
  -- Add payload with the new 'ip' field
  initiator:add_payload({ addr = addr, size = size, ip = ip })
  for cycles = 1, max_cycles do
    local data = initiator:get_read_data()
    if data then
      print(string.format("[Read] %-20s Addr=0x%x, IP=0x%x -> Cycles=%d", tag, addr, ip, cycles))
      return data, cycles
    end
    sc.start(period)
  end
  print(string.format("[TIMEOUT] %s addr=0x%x", tag, addr))
  return nil, max_cycles
end

-- Run baseline test (no prefetching)
print("\n--- Running Baseline (No Prefetching) ---")
local baseline_cycles = {}
for i = 0, NUM_ACCESSES - 1 do
  local addr = BASE_ADDR + i * STRIDE
  local _, cycles =
    read_and_measure_cycles(initiator_no_pf, addr, LOOP_PC, 4, MAX_CYCLES, "Baseline Read")
  baseline_cycles[#baseline_cycles + 1] = cycles
  assert(cycles >= MEM_LATENCY, "FAIL: Baseline read should always be a miss.")
end

-- Run PC prefetch test
print("\n--- Running PC-based Stride Prefetch Test ---")
local prefetch_cycles = {}
for i = 0, NUM_ACCESSES - 1 do
  local addr = BASE_ADDR + i * STRIDE
  local _, cycles =
    read_and_measure_cycles(initiator_pc_pf, addr, LOOP_PC, 4, MAX_CYCLES, "Prefetch Read")
  prefetch_cycles[#prefetch_cycles + 1] = cycles

  -- Give the prefetch request time to complete before the next demand read.
  if i < NUM_ACCESSES - 1 then sc.start((MEM_LATENCY + 10) * period) end
end

print("\n--- Test Verification ---")

local function sum_cycles(cycle_table)
  local total = 0
  for _, cycles in ipairs(cycle_table) do
    total = total + cycles
  end
  return total
end

local total_baseline_cycles = sum_cycles(baseline_cycles)
local total_prefetch_cycles = sum_cycles(prefetch_cycles)

print(string.format("Total Baseline Cycles: %d", total_baseline_cycles))
print(string.format("Total Prefetch Cycles: %d", total_prefetch_cycles))

-- With prefetch-on-miss, we expect at least one hit, so the total time should
-- be reduced by roughly one memory latency.
local expected_min_reduction = MEM_LATENCY - 10 -- (A miss of ~57 becomes a hit of < 10)
local actual_reduction = total_baseline_cycles - total_prefetch_cycles

assert(
  actual_reduction >= expected_min_reduction,
  string.format(
    "FAIL: Total prefetch time did not improve enough. Baseline=%d, Prefetch=%d, Reduction=%d, Expected Min Reduction=%d",
    total_baseline_cycles,
    total_prefetch_cycles,
    actual_reduction,
    expected_min_reduction
  )
)

print(
  string.format(
    "[PASS] Prefetcher reduced total execution time from %d to %d cycles.",
    total_baseline_cycles,
    total_prefetch_cycles
  )
)
