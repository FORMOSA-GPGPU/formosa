-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0
--
-- Dual-port DMA unit test.
--
-- Bench topology (one engine, two memories):
--
--                  ┌───────────┐
--                  │ Initiator │  (programs CSR / polls STATUS)
--                  └─────┬─────┘
--                        │
--        ◄───┬───────────▼───────────┬───►  xbar
--            │           │           │
--       ┌────▼────┐ ┌────▼────┐ ┌────▼────┐
--       │   DMA   │ │  mem0   │ │  mem1   │
--       │ CSR@0x0 │ │ @0x100  │ │ @0x200  │
--       └─┬────┬──┘ └─────────┘ └─────────┘
--         │    │
--         │    └── port1 ──► xbar ──► mem1
--         └────── port0 ──► xbar ──► mem0
--
-- SIZE > 0: port0 → port1 (mem0 → mem1)
-- SIZE < 0: port1 → port0 (mem1 → mem0)
--
-- System-level Host/Device DMA are two instances of this class with different
-- port bindings; this test covers the dual-port engine itself.

local initiator = simple.Initiator("initiator")
local engine = dma.DMA("dma", { fifo_size = 4 })
local mem_param = { size = 0x100, latency = 1, fifo_size = 1 }
local mem0 = simple.Memory("mem0", mem_param)
local mem1 = simple.Memory("mem1", mem_param)

local xbar = simple.XBar("xbar", 3, {
  { addr = 0x0, size = 0x28 },
  { addr = 0x100, size = 0x100 },
  { addr = 0x200, size = 0x100 },
})

local period = sc.time(10, sc.time_unit.NS)
local clock = sc.clock("clock", period)
initiator.clock = clock
xbar.clock = clock
mem0.clock = clock
mem1.clock = clock

xbar.mem_side[1].target = engine.slave_port
xbar.mem_side[2].target = mem0.port
xbar.mem_side[3].target = mem1.port
engine.port0_target = xbar.core_side[1].port
engine.port1_target = xbar.core_side[2].port
initiator.target = xbar.core_side[3].port

-- Encode a Lua number as little-endian int64/uint64 without using 2^64
-- (IEEE doubles cannot represent all integers near 2^64).
local function u64_le(value)
  local lo
  local hi
  if value >= 0 then
    lo = value % 0x100000000
    hi = math.floor(value / 0x100000000) % 0x100000000
  else
    local n = -value
    lo = n % 0x100000000
    hi = math.floor(n / 0x100000000) % 0x100000000
    -- two's complement of (hi:lo)
    lo = (0x100000000 - lo) % 0x100000000
    if lo == 0 then
      hi = (0x100000000 - hi) % 0x100000000
    else
      hi = (0x100000000 - hi - 1) % 0x100000000
    end
  end
  local bytes = {}
  for i = 1, 4 do
    bytes[i] = lo % 256
    lo = math.floor(lo / 256)
  end
  for i = 5, 8 do
    bytes[i] = hi % 256
    hi = math.floor(hi / 256)
  end
  return bytes
end

local function write64(addr, value) initiator:add_payload({ addr = addr, data = u64_le(value) }) end

local function read64(addr) initiator:add_payload({ addr = addr, size = 8 }) end

local function drain_read_data()
  local last = nil
  local data = initiator:get_read_data()
  while data do
    local v = 0
    local mul = 1
    for i = 1, 8 do
      v = v + data[i] * mul
      mul = mul * 256
    end
    last = v
    data = initiator:get_read_data()
  end
  return last
end

local function assert_bytes(actual, expected, label)
  for i = 1, #expected do
    assert(
      actual[i] == expected[i],
      string.format(
        "%s byte mismatch at %d: got 0x%02x want 0x%02x",
        label,
        i,
        actual[i],
        expected[i]
      )
    )
  end
end

local function run_copy(addr0, addr1, size, wait_cycles)
  write64(0x08, addr0)
  write64(0x10, addr1)
  write64(0x18, size)
  write64(0x00, 1) -- START
  for _ = 1, 32 do
    read64(0x20) -- STATUS
  end
  sc.start(wait_cycles * period)
  local status = drain_read_data()
  assert(
    status == 2,
    "DMA did not complete, last status=" .. tostring(status) .. " size=" .. tostring(size)
  )
end

-- Case 1: SIZE > 0, port0 → port1 (mem0 → mem1)
local pattern_fwd = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 }
mem0:write_bytes(0x0, pattern_fwd)
mem1:write_bytes(0x0, { 0, 0, 0, 0, 0, 0, 0, 0 })
run_copy(0x100, 0x200, 8, 200)
assert_bytes(mem1:read_bytes(0x0, 8), pattern_fwd, "positive SIZE mem1")

-- Case 2: SIZE < 0, port1 → port0 (mem1 → mem0)
local pattern_rev = { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x02 }
mem1:write_bytes(0x0, pattern_rev)
mem0:write_bytes(0x0, { 0, 0, 0, 0, 0, 0, 0, 0 })
run_copy(0x100, 0x200, -8, 200)
assert_bytes(mem0:read_bytes(0x0, 8), pattern_rev, "negative SIZE mem0")

print("dma unit test passed")
