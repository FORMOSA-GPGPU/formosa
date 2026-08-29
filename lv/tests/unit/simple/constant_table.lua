-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local bit = require("bit")

local period = sc.time(10, sc.time_unit.NS)
local clock = sc.clock("clock", period)

local initiator = simple.Initiator("initiator")

local tbl = {
  { addr = 0x0, size = 8, value = 0x123456789abcdef0 },
  { addr = 0x8, size = 4, value = 0xdeadbeef },
  { addr = 0xc, size = 2, value = 0xcafe },
  { addr = 0xe, size = 1, value = 0x42 },
}

local dut = simple.ConstantTable("ConstantTable", {
  entries = tbl,
})

initiator.target = dut.port
initiator.clock = clock

local function value_to_bytes_le(value, size)
  local bytes = {}
  for _ = 1, size do
    table.insert(bytes, value % 256)
    value = math.floor(value / 256)
  end
  return bytes
end

local payloads = {}
for _, entry in ipairs(tbl) do
  initiator:add_payload({ addr = entry.addr, size = entry.size })
  table.insert(payloads, {
    addr = entry.addr,
    data = value_to_bytes_le(entry.value, entry.size),
  })
end

sc.start(#payloads * 10 * period)
print("Simulation stops at", sc.time_stamp())
print("------------------------------------------------------")

local data = initiator:get_read_data()
while data do
  local payload = table.remove(payloads, 1)
  assert(payload ~= nil, "Received more read responses than expected")

  print(string.format("Checking payload 0x%x:0x%x", payload.addr, #payload.data))

  assert(#data == #payload.data, string.format("Expected size %d, got %d", #payload.data, #data))

  for k, v in ipairs(data) do
    assert(
      payload.data[k] == v,
      string.format("Mismatch at byte %d: expected 0x%02x, got 0x%02x", k, payload.data[k], v)
    )
    print(string.format("--- 0x%02x == 0x%02x", payload.data[k], v))
  end

  data = initiator:get_read_data()
end

assert(#payloads == 0, "All payloads should be checked")

print("Pass!")
