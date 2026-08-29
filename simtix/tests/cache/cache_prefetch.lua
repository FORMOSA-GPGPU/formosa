-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local MEM_SIZE = 65536
local MEM_LATENCY = 50
local period = sc.time(1, sc.time_unit.NS)
local clock = sc.clock("clock", period)

local function next_line(addr, ip, line_size)
  local DEGREE = 1
  local prefetch_addresses = {}
  for i = 1, DEGREE do
    prefetch_addresses[#prefetch_addresses + 1] = addr + i * line_size
  end
  return prefetch_addresses
end

local cache_pf = simtix.Cache("cache_pf", {
  size_bytes = 4096,
  ways = 4,
  block_size_bytes = 64,
  replacement_policy = "lru",
  write_hit_policy = "WriteBack",
  write_miss_policy = "WriteAllocate",
  prefetch_fn = next_line,
  prefetch_max = 1,
})

local cache_no_pf = simtix.Cache("cache_no_pf", {
  size_bytes = 4096,
  ways = 4,
  block_size_bytes = 64,
  replacement_policy = "lru",
  write_hit_policy = "WriteBack",
  write_miss_policy = "WriteAllocate",
  prefetch_max = 0,
})

local initiator_pf = simple.Initiator("initiator_pf")
local initiator_no_pf = simple.Initiator("initiator_no_pf")
local mmio_initiator_pf = simple.Initiator("mmio_initiator_pf")
local mmio_initiator_no_pf = simple.Initiator("mmio_initiator_no_pf")
local memory_pf = simple.Memory("memory_pf", { size = MEM_SIZE, latency = MEM_LATENCY })
local memory_no_pf = simple.Memory("memory_no_pf", { size = MEM_SIZE, latency = MEM_LATENCY })

initiator_pf.target = cache_pf.port
cache_pf.target = memory_pf.port
initiator_no_pf.target = cache_no_pf.port
cache_no_pf.target = memory_no_pf.port
mmio_initiator_pf.target = cache_pf.mmio_port
mmio_initiator_no_pf.target = cache_no_pf.mmio_port

initiator_pf.clock = clock
cache_pf.clock = clock
memory_pf.clock = clock
initiator_no_pf.clock = clock
cache_no_pf.clock = clock
memory_no_pf.clock = clock
mmio_initiator_pf.clock = clock
mmio_initiator_no_pf.clock = clock

local LINE_SIZE = 64
local ADDR_A = 0x100
local ADDR_B = ADDR_A + LINE_SIZE
local ADDR_C = ADDR_B + LINE_SIZE
local ADDR_X = 0x9000
local BASE_ADDRS = { 0x100, 0x200, 0x300 }
local MAX_MISS_CYCLES = MEM_LATENCY * 20

local DATA_A = { 0xde, 0xad, 0xbe, 0xef }
local DATA_B = { 0xaa, 0xbb, 0xcc, 0xdd }
local DATA_C = { 0x11, 0x22, 0x33, 0x44 }
local DATA_X = { 0x55, 0x66, 0x77, 0x88 }

for _, base in ipairs(BASE_ADDRS) do
  memory_pf:write_bytes(base, DATA_A)
  memory_pf:write_bytes(base + LINE_SIZE, DATA_B)
  memory_pf:write_bytes(base + 2 * LINE_SIZE, DATA_C)
end
memory_pf:write_bytes(ADDR_X, DATA_X)
for _, base in ipairs(BASE_ADDRS) do
  memory_no_pf:write_bytes(base, DATA_A)
  memory_no_pf:write_bytes(base + LINE_SIZE, DATA_B)
  memory_no_pf:write_bytes(base + 2 * LINE_SIZE, DATA_C)
end
memory_no_pf:write_bytes(ADDR_X, DATA_X)

local function read_and_measure_cycles(initiator, addr, size, max_cycles, tag)
  initiator:add_payload({ addr = addr, size = size })
  for cycles = 1, max_cycles do
    local data = initiator:get_read_data()
    if data then return data, cycles end
    sc.start(period)
  end
  print(
    string.format(
      "[TIMEOUT] %s addr=0x%x size=%d waited=%d cycles (%.3f ns)",
      tag or "",
      addr,
      size,
      max_cycles,
      max_cycles * 1.0
    )
  )
  return nil, max_cycles
end

print("======================================================")
print("Cache Prefetch Test (next-line, rigorous)")
print("======================================================")

local function assert_data(resp, exp, tag)
  assert(resp, "FAIL: timeout waiting for read response " .. tag)
  assert(#resp == #exp, "FAIL: read size mismatch " .. tag)
  for i = 1, #exp do
    assert(resp[i] == exp[i], "FAIL: read data mismatch " .. tag)
  end
end

local function print_values(label, values)
  local list = {}
  for i = 1, #values do
    list[#list + 1] = tostring(values[i])
  end
  print(string.format("%s: [%s] cycles", label, table.concat(list, ", ")))
end

local baseline_a = {}
local baseline_b = {}
local prefetch_a = {}
local prefetch_b = {}
local prefetch_c = {}
local prefetch_x1 = {}
local prefetch_x2 = {}

for _, base in ipairs(BASE_ADDRS) do
  local a = base
  local b = base + LINE_SIZE
  local c = base + 2 * LINE_SIZE

  local tag_base = string.format("base=0x%x", base)
  local resp_a, cycles_a =
    read_and_measure_cycles(initiator_no_pf, a, 4, MAX_MISS_CYCLES, "baseline A " .. tag_base)
  assert_data(resp_a, DATA_A, "(baseline A)")
  assert(cycles_a >= MEM_LATENCY, "FAIL: baseline read A should miss")
  baseline_a[#baseline_a + 1] = cycles_a

  local resp_b, cycles_b =
    read_and_measure_cycles(initiator_no_pf, b, 4, MAX_MISS_CYCLES, "baseline B " .. tag_base)
  assert_data(resp_b, DATA_B, "(baseline B)")
  assert(cycles_b >= MEM_LATENCY, "FAIL: baseline read B should miss")
  baseline_b[#baseline_b + 1] = cycles_b

  local resp_x1, cycles_x1 = read_and_measure_cycles(
    initiator_pf,
    ADDR_X,
    4,
    MAX_MISS_CYCLES,
    "prefetch X warm " .. tag_base
  )
  assert_data(resp_x1, DATA_X, "(prefetch X warm)")
  prefetch_x1[#prefetch_x1 + 1] = cycles_x1
  sc.start((MEM_LATENCY + 10) * period)

  local resp_a_pf, cycles_a_pf =
    read_and_measure_cycles(initiator_pf, a, 4, MAX_MISS_CYCLES, "prefetch A " .. tag_base)
  assert_data(resp_a_pf, DATA_A, "(prefetch A)")
  assert(cycles_a_pf >= MEM_LATENCY, "FAIL: prefetch read A should miss")
  prefetch_a[#prefetch_a + 1] = cycles_a_pf

  sc.start((MEM_LATENCY + 10) * period)

  local resp_b_pf, cycles_b_pf =
    read_and_measure_cycles(initiator_pf, b, 4, 50, "prefetch B " .. tag_base)
  assert_data(resp_b_pf, DATA_B, "(prefetch B)")
  assert(cycles_b_pf <= 10, "FAIL: prefetch read B should be fast hit")
  prefetch_b[#prefetch_b + 1] = cycles_b_pf

  local resp_c_pf, cycles_c_pf =
    read_and_measure_cycles(initiator_pf, c, 4, MAX_MISS_CYCLES, "prefetch C " .. tag_base)
  assert_data(resp_c_pf, DATA_C, "(prefetch C)")
  assert(cycles_c_pf >= MEM_LATENCY, "FAIL: prefetch should not fetch next-next line")
  prefetch_c[#prefetch_c + 1] = cycles_c_pf
  sc.start((MEM_LATENCY + 20) * period)

  local resp_x2, cycles_x2 = read_and_measure_cycles(
    initiator_pf,
    ADDR_X,
    4,
    MAX_MISS_CYCLES,
    "prefetch X verify " .. tag_base
  )
  assert_data(resp_x2, DATA_X, "(prefetch X verify)")
  assert(cycles_x2 <= 10, "FAIL: prefetch should not evict unrelated cached line")
  prefetch_x2[#prefetch_x2 + 1] = cycles_x2

  -- Drain any in-flight prefetches before next base.
  sc.start((MEM_LATENCY + 20) * period)
end

print_values("[baseline] read A", baseline_a)
print_values("[baseline] read B", baseline_b)
print_values("[prefetch] read A", prefetch_a)
print_values("[prefetch] read B", prefetch_b)
print_values("[prefetch] read C (next-next line)", prefetch_c)
print_values("[prefetch] read X warm", prefetch_x1)
print_values("[prefetch] read X verify", prefetch_x2)

local delta_b = {}
for i = 1, #baseline_b do
  delta_b[#delta_b + 1] = baseline_b[i] - prefetch_b[i]
end
print_values("Δ baseline B - prefetch B", delta_b)

print("[PASS] Prefetch hit is faster than baseline and data is correct.")
