-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- Keep every case deterministic so the expected cache-line requests can be
-- checked explicitly. Stack remap stays disabled; this test targets only LSU
-- coalescing, store byte-enable generation, and load scatter.
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
      return simtix.CoalescingLsu(name, param, {
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
    name = config.name,
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

lv.info("Pass!")
