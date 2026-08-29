-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local period = sc.time(10, sc.time_unit.NS)
local clock = sc.clock("clk", period)

local addr_map = {
  { addr = 0x10000000, size = 0x100 },
  { addr = 0x10000100, size = 0x100 },
}

---@type simple.Initiator[]
local initiators = {
  simple.Initiator("i1"),
  simple.Initiator("i2"),
}

---@type simple.Memory[]
local memories = {}

---@type dbg.Monitor[]
local monitors = {}

for _, m in ipairs(addr_map) do
  table.insert(
    memories,
    simple.Memory(string.format("mem[0x%x:x%x]", m.addr, m.size), { size = m.size })
  )
  table.insert(monitors, dbg.Monitor(string.format("mon[0x%x:0x%x]", m.addr, m.size)))
end

local xbar = simple.XBar("xbar", #initiators, addr_map)
xbar.clock = clock

for n, i in ipairs(initiators) do
  i.clock = clock
  i.target = xbar.core_side[n].port
end

for n, m in ipairs(memories) do
  m.clock = clock
  xbar.mem_side[n].target = monitors[n].from
  monitors[n].to = m.port
end

local payloads = {
  {
    { addr = 0x10000100, data = { 0xab, 0xcd } },
    { addr = 0x10000104, data = { 0x12, 0x34 } },
    { addr = 0x10000008, data = { 0xde, 0xad, 0xbe, 0xbf } },
    { addr = 0x1000000c, data = { 0xff } },
    { addr = 0x20000000, data = { 0xff, 0xee, 0xdd, 0xcc } }, --- Error payload
  },
  {
    { addr = 0x10000040, data = { 0x11, 0x22 } },
    { addr = 0x10000044, data = { 0xaa, 0xbb, 0xcc, 0xdd } },
    { addr = 0x10000148, data = { 0xfb, 0xfb } },
    { addr = 0x1000014c, data = { 0x00 } },
    { addr = 0x20000100, data = { 0x11, 0x22, 0x33, 0x44 } }, --- Error payload
  },
}

for n, i in ipairs(initiators) do
  for _, payload in ipairs(payloads[n]) do
    i:add_payload(payload)
  end
end

print("Writing...")
sc.start((#payloads[1] + #payloads[2]) * 10 * period)
print(sc.time_stamp())

for n, i in ipairs(initiators) do
  for _, payload in ipairs(payloads[#payloads + 1 - n]) do
    i:add_payload({ addr = payload.addr, size = #payload.data })
  end
end

print("Reading...")
sc.start((#payloads[1] + #payloads[2]) * 10 * period)
print(sc.time_stamp())

for n, i in ipairs(initiators) do
  local data = i:get_read_data()
  local golden = payloads[#payloads + 1 - n]

  while data do
    -- Take and remove the very first element in the payloads
    local payload = table.remove(golden, 1)
    print(string.format("Checking payload 0x%x:0x%x", payload.addr, #payload.data))

    -- The data read out must be the same as written in
    for k, v in ipairs(data) do
      print(string.format("--- 0x%02x == 0x%02x", payload.data[k], data[k]))
      assert(payload.data[k] == v)
    end

    data = i:get_read_data()
  end

  -- After we remove all of them one by one,
  -- the left golden payloads should leave only one (the error one)
  assert(#golden == 1)
end

print("Pass!")
