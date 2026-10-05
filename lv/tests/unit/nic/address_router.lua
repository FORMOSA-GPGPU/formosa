-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local period = sc.time(1, sc.time_unit.NS)
local clock = sc.clock("clock", period)
-- Deliberately unsorted: target binding order is Lua table order.
local xbar = nic.XBar("xbar", 3, {
  { addr = 0x3000ULL, size = 0x100ULL, subtract_start_addr = false },
  { addr = 0x1000, size = 0x100 },
})
local routers = {
  nic.Router("high", { { addr = 0x3000, size = 0x100 } }),
  nic.Router("low", {
    { addr = 0, size = 0x80 },
    { addr = 0x80, size = 0x80, subtract_start_addr = false },
  }),
}
for _, router in ipairs(routers) do
  xbar.mem_side = router.from
end

local memories = {}
for i = 1, 3 do
  memories[i] = simple.Memory("memory" .. i, { size = 0x100, latency = i * 3, fifo_size = 1 })
  memories[i].clock = clock
end
routers[1].to = memories[1].port
routers[2].to = memories[2].port
routers[2].to = memories[3].port

local initiators = {}
for i = 1, 2 do
  initiators[i] = simple.OutstandingInitiator("initiator" .. i)
  initiators[i].target = xbar.core_side
end
local debugger = dbg.MemoryDebugger("debugger")
debugger.target = xbar.core_side

local function equal(actual, expected)
  assert(actual and #actual == #expected, "missing or wrong-sized response")
  for i, value in ipairs(expected) do
    assert(actual[i] == value, "incorrect routing or data")
  end
end

local function completed(count)
  for _ = 1, 4096 do
    if initiators[1]:completed_count() == count and initiators[2]:completed_count() == count then
      return
    end
    sc.start(period)
  end
  error("timed out waiting for all responses")
end

sc.start(sc.ZERO_TIME)
for _, addr in ipairs({ 0x3000, 0x1000, 0x1080, 0x30ff, 0x107f, 0x10ff }) do
  assert(debugger:write_bytes(addr, { 0xab }) == 1)
  equal(debugger:read_bytes(addr, 1), { 0xab })
end
for _, addr in ipairs({ 0x2000, 0x0fff, 0x30ff, 0x107f, 0x10ff }) do
  assert(debugger:write_bytes(addr, { 1, 2 }) == 0, "unmapped/cross-region write accepted")
end

local bases = { 0x3000, 0x1000, 0x1080 }
-- More requests than the existing TlmSource FIFO capacity, with contention on
-- all targets and different response latencies.
for master, initiator in ipairs(initiators) do
  for n = 0, 47 do
    local target = n % 3 + 1
    local offset = math.floor(n / 3) * 4 + (master - 1) * 2
    initiator:add_payload({ addr = bases[target] + offset, data = { master, n } })
  end
end
completed(48)
for master, initiator in ipairs(initiators) do
  for n = 0, 47 do
    local target = n % 3 + 1
    local offset = math.floor(n / 3) * 4 + (master - 1) * 2
    local local_addr = offset + (target == 3 and 0x80 or 0)
    equal(memories[target]:read_bytes(local_addr, 2), { master, n })
    initiator:add_payload({ addr = bases[target] + offset, size = 2 })
  end
end
completed(96)
for master, initiator in ipairs(initiators) do
  local seen = {}
  for _ = 1, 48 do
    local data = initiator:get_read_data()
    assert(data and #data == 2 and data[1] == master, "response sent to wrong master")
    assert(data[2] < 48 and not seen[data[2]], "duplicate or corrupt response")
    seen[data[2]] = true
  end
  assert(initiator:get_read_data() == nil)
  initiator:add_payload({ addr = 0x2000, size = 1 })
  initiator:add_payload({ addr = 0x10ff, size = 2 })
end
completed(98)
for _, initiator in ipairs(initiators) do
  assert(initiator:get_read_data() == nil, "invalid reads must return address errors")
end
print("Pass!")
