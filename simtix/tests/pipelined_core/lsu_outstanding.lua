-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- Keep every case deterministic so the expected cache-line requests can be
-- checked explicitly. Stack remap stays disabled; this test targets only LSU
-- coalescing, store byte-enable generation, and load scatter.
local slots = 4

local MEM_SIZE = 16 * 1024

local CONFIGS = {
  { name = "lanes4_line32", num_lanes = 4, cache_block_size = 32, widths = { 1, 2, 4, 8 } },
  { name = "lanes8_line64", num_lanes = 8, cache_block_size = 64, widths = { 1, 2, 4, 8 } },
  { name = "lanes32_line64", num_lanes = 32, cache_block_size = 64, widths = { 1, 2, 4, 8 } },
}

local period = sc.time(1, sc.time_unit.NS)
local clock = sc.clock("clock", period)

local function fail(msg) error(msg, 2) end

local function assert_eq(actual, expected, msg)
  if actual ~= expected then
    fail(string.format("%s: expected %s, got %s", msg, tostring(expected), tostring(actual)))
  end
end

local function assert_byte(actual, expected, msg)
  if actual ~= expected then
    fail(string.format("%s: expected 0x%02x, got 0x%02x", msg, expected, actual or -1))
  end
end

local function assert_bytes(actual, expected, msg)
  assert_eq(#actual, #expected, msg .. " length")
  for i = 1, #expected do
    assert_byte(actual[i], expected[i], string.format("%s[%d]", msg, i - 1))
  end
end

local function ones(num_lanes) return string.rep("1", num_lanes) end

local function zeros(num_lanes) return string.rep("0", num_lanes) end

-- sc_bv_base parses the rightmost character as lane 0. Build masks from lane
-- numbers so the tests can reason in the same lane order as the LSU loops.
local function tmask_from_predicate(num_lanes, predicate)
  local bits = {}
  for i = 1, num_lanes do
    bits[i] = "0"
  end
  for lane = 1, num_lanes do
    if predicate(lane) then bits[num_lanes - lane + 1] = "1" end
  end
  return table.concat(bits)
end

local function active_lanes(fixture, tmask)
  local lanes = {}
  for lane = 1, fixture.num_lanes do
    local bit_index = #tmask - lane + 1
    if tmask:sub(bit_index, bit_index) == "1" then table.insert(lanes, lane) end
  end
  return lanes
end

local function line_addr(fixture, addr) return addr - (addr % fixture.cache_block_size) end

local function line_offset(fixture, addr) return addr % fixture.cache_block_size end

local function line_bases_for(fixture, addrs, tmask)
  local lines = {}
  local seen = {}
  for _, lane in ipairs(active_lanes(fixture, tmask)) do
    local base = line_addr(fixture, addrs[lane])
    if not seen[base] then
      table.insert(lines, base)
      seen[base] = true
    end
  end
  return lines
end

local function pattern_line(fixture, seed)
  local line = {}
  for byte = 1, fixture.cache_block_size do
    line[byte] = (seed + byte * 0x0b) % 256
  end
  return line
end

-- LsuTester stores eight bytes per lane; narrow accesses use the low bytes.
local function make_lane_data(fixture, seed)
  local data = {}
  local lanes = {}
  for lane = 1, fixture.num_lanes do
    lanes[lane] = {}
    for byte = 1, 8 do
      local value = (seed + lane * 0x13 + byte * 0x07) % 256
      lanes[lane][byte] = value
      table.insert(data, value)
    end
  end
  return data, lanes
end

-- Build the request payload, byte-enable mask, and final backing-memory line
-- expected from a coalesced store request for one cache line. Disabled bytes in
-- the request payload stay zero, while disabled bytes in memory keep sentinel.
local function expected_store_line(fixture, addrs, lane_bytes, width, line_base, tmask, sentinel)
  local request_data = {}
  local byte_enable = {}
  local backing_data = {}
  for i = 1, fixture.cache_block_size do
    request_data[i] = 0
    byte_enable[i] = 0
    backing_data[i] = sentinel[i]
  end

  for _, lane in ipairs(active_lanes(fixture, tmask)) do
    local offset = addrs[lane] - line_base
    if offset >= 0 and offset + width <= fixture.cache_block_size then
      for byte = 1, width do
        local index = offset + byte
        request_data[index] = lane_bytes[lane][byte]
        byte_enable[index] = 0xff
        backing_data[index] = lane_bytes[lane][byte]
      end
    end
  end

  return request_data, byte_enable, backing_data
end

local function context(fixture, scenario, width, mask_name)
  return string.format("%s/%s/width%d/%s", fixture.name, scenario, width, mask_name)
end

local function assert_store_request(fixture, index, line_base, expected_data, expected_be, msg)
  local mem = fixture.mem
  assert_eq(mem:request_command(index), "write", msg .. " request command")
  assert_eq(mem:request_addr(index), line_base, msg .. " request address")
  assert_eq(mem:request_length(index), fixture.cache_block_size, msg .. " request length")
  assert_bytes(mem:request_data(index), expected_data, msg .. " request data")
  assert_bytes(mem:request_byte_enable(index), expected_be, msg .. " request byte-enable")
end

local next_ip = 0x1000
local function alloc_ip()
  local ip = next_ip
  next_ip = next_ip + 4
  return ip
end

local function run_store_case(fixture, scenario, addrs, width, tmask, mask_name, seed)
  local mem = fixture.mem
  local lines = line_bases_for(fixture, addrs, tmask)
  local sentinels = {}
  local data, lane_bytes = make_lane_data(fixture, seed)
  local msg = context(fixture, scenario, width, mask_name)

  for index, base in ipairs(lines) do
    sentinels[base] = pattern_line(fixture, 0x51 + index * 0x31)
    mem:write_bytes(base, sentinels[base])
  end

  mem:clear_requests()
  fixture.tester:store(alloc_ip(), addrs, data, width, tmask)

  assert_eq(mem:num_requests(), #lines, msg .. " request count")
  for index, base in ipairs(lines) do
    local expected_data, expected_be, expected_memory =
      expected_store_line(fixture, addrs, lane_bytes, width, base, tmask, sentinels[base])
    assert_store_request(fixture, index, base, expected_data, expected_be, msg)
    assert_bytes(
      mem:read_bytes(base, fixture.cache_block_size),
      expected_memory,
      msg .. " backing memory"
    )
  end
end

local function force_sign_bits(fixture, addrs, tmask, width, line_data)
  if width == 8 then return end
  for _, lane in ipairs(active_lanes(fixture, tmask)) do
    local base = line_addr(fixture, addrs[lane])
    local offset = line_offset(fixture, addrs[lane])
    line_data[base][offset + width] = 0x80 + (lane % 0x40)
  end
end

local function run_load_case(fixture, scenario, addrs, width, is_signed, tmask, mask_name, seed)
  local mem = fixture.mem
  local lines = line_bases_for(fixture, addrs, tmask)
  local line_data = {}
  local msg = context(fixture, scenario, width, mask_name)

  for index, base in ipairs(lines) do
    line_data[base] = pattern_line(fixture, seed + index * 0x21)
    mem:write_bytes(base, line_data[base])
  end
  if is_signed then
    force_sign_bits(fixture, addrs, tmask, width, line_data)
    for _, base in ipairs(lines) do
      mem:write_bytes(base, line_data[base])
    end
  end

  mem:clear_requests()
  local result = fixture.tester:load(alloc_ip(), addrs, width, is_signed, tmask)

  assert_eq(mem:num_requests(), #lines, msg .. " request count")
  for index, base in ipairs(lines) do
    assert_eq(mem:request_command(index), "read", msg .. " request command")
    assert_eq(mem:request_addr(index), base, msg .. " request address")
    assert_eq(mem:request_length(index), fixture.cache_block_size, msg .. " request length")
    assert_bytes(mem:request_data(index), line_data[base], msg .. " line data")
  end

  for _, lane in ipairs(active_lanes(fixture, tmask)) do
    local base = line_addr(fixture, addrs[lane])
    local offset = line_offset(fixture, addrs[lane])
    local sign_byte = 0
    if is_signed and width < 8 and line_data[base][offset + width] >= 0x80 then sign_byte = 0xff end
    for byte = 1, 8 do
      local expected = sign_byte
      if byte <= width then expected = line_data[base][offset + byte] end
      local actual = result[(lane - 1) * 8 + byte]
      assert_byte(actual, expected, string.format("%s lane %d byte %d", msg, lane - 1, byte - 1))
    end
  end
end

local same_line_addrs

local function run_zero_active_case(fixture, width)
  local tmask = zeros(fixture.num_lanes)
  local addrs = same_line_addrs(fixture, width, 0x380)
  local line = pattern_line(fixture, 0x9d)
  local data = make_lane_data(fixture, 0x19)
  local msg = context(fixture, "zero-active", width, "none")

  fixture.mem:write_bytes(0x380, line)
  fixture.mem:clear_requests()
  fixture.tester:store(alloc_ip(), addrs, data, width, tmask)
  assert_eq(fixture.mem:num_requests(), 0, msg .. " store request count")
  assert_bytes(
    fixture.mem:read_bytes(0x380, fixture.cache_block_size),
    line,
    msg .. " store backing memory"
  )

  fixture.mem:clear_requests()
  fixture.tester:load(alloc_ip(), addrs, width, false, tmask)
  assert_eq(fixture.mem:num_requests(), 0, msg .. " load request count")
end

same_line_addrs = function(fixture, width, base)
  local addrs = {}
  for lane = 1, fixture.num_lanes do
    local max_offset = fixture.cache_block_size - width
    addrs[lane] = base + (((lane - 1) * width) % (max_offset + 1))
  end
  return addrs
end

local function two_line_addrs(fixture, width, base)
  local addrs = {}
  local split_lane = math.floor(fixture.num_lanes / 2)
  for lane = 1, fixture.num_lanes do
    local line_base = base
    if lane > split_lane then line_base = base + fixture.cache_block_size end
    local max_offset = fixture.cache_block_size - width
    addrs[lane] = line_base + (((lane - 1) * width) % (max_offset + 1))
  end
  return addrs
end

local function different_line_addrs(fixture, width, base)
  local addrs = {}
  local offset = fixture.cache_block_size - width
  for lane = 1, fixture.num_lanes do
    addrs[lane] = base + (lane - 1) * fixture.cache_block_size + offset
  end
  return addrs
end

local function make_fixture(config)
  local param = {
    num_lanes = config.num_lanes,
    num_warps = 1,
  }

  local tester = simtix.LsuTester("tester_" .. config.name, param)
  tester:lsu_init(
    function(name)
      return simtix.CoalescingOutstandingLsu(name, param, {
        num_inflight_slots = slots,
        cache_block_size = config.cache_block_size,
        enable_stack_remap = false,
      })
    end
  )

  -- LsuProbeMemory records the line transactions issued by the LSU and also
  -- behaves as the backing memory used to verify store/load results.
  local mem = simtix.LsuProbeMemory("probe_" .. config.name, {
    size = MEM_SIZE,
  })

  tester.clock = clock
  tester.target = mem.port

  local fixture = {
    name = config.name .. "/slots" .. slots,
    num_lanes = config.num_lanes,
    cache_block_size = config.cache_block_size,
    widths = config.widths,
    tester = tester,
    mem = mem,
  }
  fixture.masks = {
    { name = "all", value = ones(config.num_lanes) },
    {
      name = "odd-lanes",
      value = tmask_from_predicate(config.num_lanes, function(lane) return lane % 2 == 1 end),
    },
  }
  return fixture
end

local fixtures = {}
for _, config in ipairs(CONFIGS) do
  table.insert(fixtures, make_fixture(config))
end

-- Construct the concurrent-response fixture before SystemC starts.
local outstanding_tester, outstanding_mem
do
  local lanes, line = 4, 32
  local param = { num_lanes = lanes, num_warps = 2 }
  outstanding_tester = simtix.LsuTester("outstanding_tester", param)
  outstanding_tester:lsu_init(
    function(name)
      return simtix.CoalescingOutstandingLsu(name, param, {
        cache_block_size = line,
        enable_stack_remap = false,
        num_inflight_slots = slots,
      })
    end
  )
  outstanding_mem = simtix.LsuProbeMemory("outstanding_mem", { size = 1024, fifo_size = 32 })
  outstanding_mem.auto_respond = false
  outstanding_tester.clock = clock
  outstanding_tester.target = outstanding_mem.port
end

sc.start(sc.ZERO_TIME)

for _, fixture in ipairs(fixtures) do
  for _, width in ipairs(fixture.widths) do
    run_zero_active_case(fixture, width)

    for _, mask in ipairs(fixture.masks) do
      -- All active lanes hit one cache line, so the LSU should emit one line
      -- request even when many lanes overlap within that line.
      run_store_case(
        fixture,
        "store-same-line",
        same_line_addrs(fixture, width, 0x040),
        width,
        mask.value,
        mask.name,
        0x01
      )
      run_load_case(
        fixture,
        "load-same-line",
        same_line_addrs(fixture, width, 0x240),
        width,
        false,
        mask.value,
        mask.name,
        0x41
      )

      -- Active lanes span two cache lines; request order should follow the
      -- first active lane that touches each line, and scatter must use the
      -- lane-to-line mapping for both reused line responses.
      run_store_case(
        fixture,
        "store-two-lines",
        two_line_addrs(fixture, width, 0x480),
        width,
        mask.value,
        mask.name,
        0x81
      )
      run_load_case(
        fixture,
        "load-two-lines",
        two_line_addrs(fixture, width, 0x600),
        width,
        false,
        mask.value,
        mask.name,
        0xa1
      )

      -- Worst-case coalescing: each active lane maps to a different line. The
      -- offset is the last legal byte range in the line, which also exercises
      -- the non-crossing boundary.
      run_store_case(
        fixture,
        "store-different-lines",
        different_line_addrs(fixture, width, 0x800),
        width,
        mask.value,
        mask.name,
        0xc1
      )
      run_load_case(
        fixture,
        "load-different-lines",
        different_line_addrs(fixture, width, 0x1000),
        width,
        false,
        mask.value,
        mask.name,
        0x101
      )

      if width < 8 then
        run_load_case(
          fixture,
          "load-signed-different-lines",
          different_line_addrs(fixture, width, 0x1800),
          width,
          true,
          mask.value,
          mask.name,
          0x141
        )
      end
    end
  end
end

do
  local tester, mem = outstanding_tester, outstanding_mem
  local lanes, line = 4, 32
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

  -- Fill all four slots, then check that a fifth request waits for a response.
  mem:clear_requests()
  for i = 1, 5 do
    local addr = 0x100 + (i - 1) * line
    tester:issue_load(0x20 + i, { addr, addr + 4, addr + 8, addr + 12 }, 1, false, "1111")
  end
  sc.start(10 * period)
  eq(mem:pending_response_count(), 4, "four slots are occupied")
  eq(mem:num_requests(), 4, "fifth load waits for a free slot")
  local occupied = {}
  for i = 1, 4 do
    local slot = mem:request_slot_id(i)
    if occupied[slot] then error("four loads must occupy distinct slots") end
    occupied[slot] = true
  end
  mem:respond(1)
  sc.start(10 * period)
  eq(tester:completed_id(), 0x21, "first load releases its slot")
  tester:collect_response()
  sc.start(10 * period)
  eq(mem:num_requests(), 5, "fifth load enters after a slot is released")
  eq(mem:pending_response_count(), 4, "four loads remain in flight")
  for _ = 1, 4 do
    mem:respond(1)
    sc.start(period)
    tester:collect_response()
  end
  eq(mem:pending_response_count(), 0, "capacity test drains all responses")
end

lv.info("Pass!")
