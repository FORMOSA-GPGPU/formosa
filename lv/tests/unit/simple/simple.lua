-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local bit = require("bit")

local initiator = simple.Initiator("initiator")

local memory = simple.Memory("memory", {
  size = 1024,
  latency = 2,
  fifo_size = 1,
})

local monitor = dbg.Monitor("monitor")
initiator.target = monitor.from
monitor.to = memory.port

local period = sc.time(10, sc.time_unit.NS)
local clock = sc.clock("clock", period)
initiator.clock = clock
memory.clock = clock
assert(memory.clock == initiator.clock, "Clock must be the same")

local function random_paylod()
  math.randomseed(os.time())
  local sizes = { 1, 2, 4, 8, 16, 32, 64 }
  local size = sizes[math.random(#sizes)] -- randomly select one
  local addr = bit.band(math.random(0, memory.size - size), bit.bnot(0xff))
  local data = {}
  for _ = 1, size do
    local byte = bit.band(math.random(0, 0xff), 0xff) -- Make sure byte is 8-bit wide
    table.insert(data, byte)
  end
  return { addr = addr, data = data } -- write payload
end

-- Generate 20 random payload to write
local payloads = {}
for _ = 1, 20 do
  local payload = random_paylod()
  initiator:add_payload(payload)
  initiator:add_payload({ addr = payload.addr, size = #payload.data })
  table.insert(payloads, payload)
end

sc.start(#payloads * 10 * period)
print("Simulation stops at", sc.time_stamp())
print("------------------------------------------------------")

-- Get read data from the initiator while available
local data = initiator:get_read_data()
while data do
  -- Take and remove the very first element in the payloads
  local payload = table.remove(payloads, 1)
  print(string.format("Checking payload 0x%x:0x%x", payload.addr, #payload.data))

  -- The data read out must be the same as written in
  for k, v in ipairs(data) do
    assert(payload.data[k] == v)
    print(string.format("--- 0x%02x == 0x%02x", payload.data[k], data[k]))
  end

  -- Get another read data
  data = initiator:get_read_data()
end

-- The payloads must be empty since we remove all of them one by one
assert(#payloads == 0)

print("Pass!")
