-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local lanes, line = 4, 32
local period = sc.time(1, sc.time_unit.NS)
local clock = sc.clock("clock", period)
local param = { num_lanes = lanes, num_warps = 2 }
local tester = simtix.LsuTester("tester", param)
tester:lsu_init(
  function(name)
    return simtix.CoalescingOutstandingLsu(name, param, {
      cache_block_size = line,
      enable_stack_remap = false,
      num_inflight_slots = 2,
    })
  end
)
local mem = simtix.LsuProbeMemory("mem", { size = 1024, fifo_size = 32 })
mem.auto_respond = false
tester.clock = clock
tester.target = mem.port

local function eq(a, b, msg)
  if a ~= b then error(string.format("%s: expected %s, got %s", msg, b, a), 2) end
end
local function fill(base, seed)
  local data = {}
  for i = 1, line do
    data[i] = (seed + i - 1) % 256
  end
  mem:write_bytes(base, data)
end

sc.start(sc.ZERO_TIME)
fill(0x100, 0x10)
fill(0x120, 0x40)
fill(0x200, 0x80)
fill(0x220, 0xc0)
local a = { 0x100, 0x120, 0x104, 0x124 }
local b = { 0x200, 0x220, 0x204, 0x224 }
tester:issue_load(0xa, a, 1, false, "1111")
tester:issue_load(0xb, b, 1, false, "1111")
sc.start(10 * period)
eq(mem:pending_response_count(), 4, "two slots must be outstanding")
local slot_a, slot_b = mem:request_slot_id(1), mem:request_slot_id(3)
if slot_a == slot_b then error("instructions must use distinct slots") end

-- Cross-slot and within-slot reorder: B.1, A.0, B.0, A.1.
mem:respond_by_tag(slot_b, 1)
sc.start(period)
eq(tester:response_available(), false, "partial B response")
mem:respond_by_tag(slot_a, 0)
sc.start(period)
eq(tester:response_available(), false, "partial A response")
local partial = tester:inspect_data(0xa)
eq(partial[1], 0, "A waits for all responses before scatter")
eq(partial[2], 0, "A unresponded lane remains initialized")
mem:respond_by_tag(slot_b, 0)
sc.start(period)
eq(tester:completed_id(), 0xb, "B completes before A")
local bd = tester:collect_response()
eq(bd[1], 0x80, "B lane 0")
eq(bd[9], 0xc0, "B lane 1")
mem:respond_by_tag(slot_a, 1)
sc.start(period)
eq(tester:completed_id(), 0xa, "A completes second")
local ad = tester:collect_response()
eq(ad[1], 0x10, "A lane 0")
eq(ad[9], 0x40, "A lane 1")
eq(mem:pending_response_count(), 0, "all responses consumed")

-- Two stores can be outstanding and complete out of issue order.
mem:clear_requests()
local store_a_data = {
  0xa0,
  0xa1,
  0xa2,
  0xa3,
  0xa4,
  0xa5,
  0xa6,
  0xa7,
  0xb0,
  0xb1,
  0xb2,
  0xb3,
  0xb4,
  0xb5,
  0xb6,
  0xb7,
  0xc0,
  0xc1,
  0xc2,
  0xc3,
  0xc4,
  0xc5,
  0xc6,
  0xc7,
  0xd0,
  0xd1,
  0xd2,
  0xd3,
  0xd4,
  0xd5,
  0xd6,
  0xd7,
}
local store_b_data = {
  0x10,
  0x11,
  0x12,
  0x13,
  0x14,
  0x15,
  0x16,
  0x17,
  0x20,
  0x21,
  0x22,
  0x23,
  0x24,
  0x25,
  0x26,
  0x27,
  0x30,
  0x31,
  0x32,
  0x33,
  0x34,
  0x35,
  0x36,
  0x37,
  0x40,
  0x41,
  0x42,
  0x43,
  0x44,
  0x45,
  0x46,
  0x47,
}
local store_a_addr = { 0x300, 0x320, 0x304, 0x324 }
local store_b_addr = { 0x380, 0x3a0, 0x384, 0x3a4 }
tester:issue_store(0xf, store_a_addr, store_a_data, 2, "1111")
tester:issue_store(0x10, store_b_addr, store_b_data, 2, "1111")
sc.start(10 * period)
eq(mem:pending_response_count(), 4, "two stores must be outstanding")
eq(mem:num_requests(), 4, "each store coalesces into two writes")
for i = 1, 4 do
  eq(mem:request_command(i), "write", "store request command")
  eq(mem:request_length(i), line, "store request line length")
end
local store_slot_a, store_slot_b = mem:request_slot_id(1), mem:request_slot_id(3)
if store_slot_a == store_slot_b then error("stores must use distinct slots") end

-- Each line contains two active 2-byte lanes; all other bytes stay disabled.
for i = 1, 4 do
  local strb = mem:request_byte_enable(i)
  for byte = 1, line do
    local enabled = byte <= 2 or (byte >= 5 and byte <= 6)
    eq(strb[byte], enabled and 0xff or 0, "store byte enable")
  end
end

local function check_store(addr, data, msg)
  for lane = 1, lanes do
    local actual = mem:read_bytes(addr[lane], 2)
    eq(actual[1], data[(lane - 1) * 8 + 1], msg .. " lane byte 0")
    eq(actual[2], data[(lane - 1) * 8 + 2], msg .. " lane byte 1")
  end
end
check_store(store_a_addr, store_a_data, "store A data")
check_store(store_b_addr, store_b_data, "store B data")

-- Cross-slot and within-slot reorder: B.1, A.0, B.0, A.1.
mem:respond_by_tag(store_slot_b, 1)
sc.start(period)
eq(tester:response_available(), false, "partial store B response")
mem:respond_by_tag(store_slot_a, 0)
sc.start(period)
eq(tester:response_available(), false, "partial store A response")
mem:respond_by_tag(store_slot_b, 0)
sc.start(period)
eq(tester:completed_id(), 0x10, "store B completes before store A")
tester:collect_response()
mem:respond_by_tag(store_slot_a, 1)
sc.start(period)
eq(tester:completed_id(), 0xf, "store A completes second")
tester:collect_response()
eq(mem:pending_response_count(), 0, "all store responses consumed")

-- Strict atomic drain barrier and lane-tag routing.
local atomic_data = {}
for i = 1, 8 * lanes do
  atomic_data[i] = i
end
tester:issue_load(0xc, { 0x100, 0x104, 0x108, 0x10c }, 1, false, "1111")
tester:issue_atomic(0xd, { 0x200, 0x204, 0x208, 0x20c }, atomic_data, 4, false, "0011", "add")
tester:issue_load(0xe, { 0x220, 0x224, 0x228, 0x22c }, 1, false, "1111")
sc.start(10 * period)
eq(mem:pending_response_count(), 1, "atomic waits for prior slot to drain")
mem:respond(1)
sc.start(period)
eq(tester:completed_id(), 0xc, "prior load completes before atomic")
tester:collect_response()
sc.start(10 * period)
eq(mem:pending_response_count(), 2, "atomic emits one request per active lane")
local atomic_slot = mem:request_slot_id(mem:num_requests())
mem:respond_by_tag(atomic_slot, 1)
sc.start(period)
eq(tester:response_available(), false, "partial atomic response")
mem:respond_by_tag(atomic_slot, 0)
sc.start(period)
eq(tester:completed_id(), 0xd, "atomic completes after all lanes")
tester:collect_response()
sc.start(10 * period)
eq(mem:pending_response_count(), 1, "following load enters after atomic completion")
mem:respond(1)
sc.start(period)
eq(tester:completed_id(), 0xe, "following load completes after atomic")
tester:collect_response()
