-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local LINE = 64
local NUM_BANKS = 4
local ADDRS = { 0x00, 0x40, 0x80, 0xC0 }
local DATA = {
  { 0xa1, 0xa2, 0xa3, 0xa4 },
  { 0xb1, 0xb2, 0xb3, 0xb4 },
  { 0xc1, 0xc2, 0xc3, 0xc4 },
  { 0xd1, 0xd2, 0xd3, 0xd4 },
}
local DBG_DATA = {
  { 0x11, 0x22, 0x33, 0x44 },
  { 0x55, 0x66, 0x77, 0x88 },
  { 0x99, 0xaa, 0xbb, 0xcc },
  { 0xdd, 0xee, 0xff, 0x01 },
}

local period = sc.time(1, sc.time_unit.NS)
local clock = sc.clock("clock", period)

local function bytes_equal(actual, expected)
  if actual == nil or expected == nil or #actual ~= #expected then return false end
  for i = 1, #expected do
    if actual[i] ~= expected[i] then return false end
  end
  return true
end

local function format_bytes(bytes)
  local parts = {}
  for i = 1, #bytes do
    parts[i] = string.format("%02x", bytes[i])
  end
  return table.concat(parts, " ")
end

local function assert_bytes(actual, expected, label)
  assert(
    bytes_equal(actual, expected),
    string.format(
      "%s: got [%s] expected [%s]",
      label,
      format_bytes(actual or {}),
      format_bytes(expected)
    )
  )
end

local function bank_id(addr) return math.floor(addr / LINE) % NUM_BANKS end

local function bank_local(addr) return math.floor(addr / (LINE * NUM_BANKS)) * LINE + (addr % LINE) end

local function expected_bank_addr(addr, preserve)
  if preserve then return addr end
  return bank_local(addr)
end

local function wait_completed(initiator, target, label)
  for _ = 1, 256 do
    if initiator:completed_count() >= target then return end
    sc.start(period)
  end
  error(
    string.format(
      "%s: timed out waiting for completions got=%d expected=%d",
      label,
      initiator:completed_count(),
      target
    )
  )
end

---@param name string
---@param preserve boolean
local function make_fixture(name, preserve)
  local router = nic.BankingRouter(name .. "_router", {
    num_froms = 2,
    total_size = 1024,
    num_tos = NUM_BANKS,
    bank_line_size = LINE,
    preserve_original_address = preserve,
  })
  local initiator = simple.Initiator(name .. "_initiator")
  local debugger = dbg.MemoryDebugger(name .. "_dbg")
  local memories = {}
  for i = 1, NUM_BANKS do
    local memory = simple.Memory(string.format("%s_bank%d", name, i - 1), {
      size = 1024,
      latency = 2,
      fifo_size = 1,
    })
    memory.clock = clock
    memories[i] = memory
    router.to = memory.port
  end
  initiator.clock = clock
  initiator.target = router.from
  debugger.target = router.from
  return {
    initiator = initiator,
    debugger = debugger,
    memories = memories,
    preserve = preserve,
  }
end

local function check_bank_store(memories, bank, addr, data, label)
  assert_bytes(
    memories[bank]:read_bytes(addr, #data),
    data,
    string.format("%s bank%d @ 0x%x", label, bank - 1, addr)
  )
end

local function check_preserve_isolation(memories, dest_bank, addr, label)
  for i, memory in ipairs(memories) do
    if i ~= dest_bank then
      local stored = memory:read_bytes(addr, 4)
      for _, byte in ipairs(stored) do
        assert(
          byte == 0,
          string.format("%s: bank%d unexpectedly stored 0x%02x at 0x%x", label, i - 1, byte, addr)
        )
      end
    end
  end
end

local function test_transport_dbg(fixture, label)
  for i, addr in ipairs(ADDRS) do
    local bank = bank_id(addr) + 1
    local stored_at = expected_bank_addr(addr, fixture.preserve)
    local written = fixture.debugger:write_bytes(addr, DBG_DATA[i])
    assert(written == #DBG_DATA[i], string.format("%s dbg-write 0x%x size", label, addr))
    check_bank_store(fixture.memories, bank, stored_at, DBG_DATA[i], label .. " dbg-write")
    if fixture.preserve then
      check_preserve_isolation(fixture.memories, bank, addr, label .. " dbg-write")
    end
    assert_bytes(
      fixture.debugger:read_bytes(addr, #DBG_DATA[i]),
      DBG_DATA[i],
      string.format("%s dbg-read 0x%x", label, addr)
    )
  end
end

local function test_normal_transport(fixture, label)
  local initiator = fixture.initiator
  for i, addr in ipairs(ADDRS) do
    local target = initiator:completed_count() + 1
    initiator:add_payload({ addr = addr, data = DATA[i] })
    wait_completed(initiator, target, string.format("%s write 0x%x", label, addr))
  end
  sc.start(64 * period)

  for i, addr in ipairs(ADDRS) do
    local bank = bank_id(addr) + 1
    local stored_at = expected_bank_addr(addr, fixture.preserve)
    check_bank_store(fixture.memories, bank, stored_at, DATA[i], label .. " nb-write")
    if fixture.preserve then
      check_preserve_isolation(fixture.memories, bank, addr, label .. " nb-write")
    end
  end

  for i, addr in ipairs(ADDRS) do
    local target = initiator:completed_count() + 1
    initiator:add_payload({ addr = addr, size = #DATA[i] })
    wait_completed(initiator, target, string.format("%s read 0x%x", label, addr))
    local actual = initiator:get_read_data()
    assert_bytes(actual, DATA[i], string.format("%s nb-read 0x%x", label, addr))
  end
end

local local_addr = make_fixture("local", false)
local preserved = make_fixture("preserve", true)

-- Finish SystemC elaboration before using transport_dbg.
sc.start(sc.ZERO_TIME)

test_transport_dbg(local_addr, "bank-local")
test_transport_dbg(preserved, "preserve")
test_normal_transport(local_addr, "bank-local")
test_normal_transport(preserved, "preserve")

print("Pass!")
