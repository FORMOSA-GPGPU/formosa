-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local period = sc.time(10, sc.time_unit.NS)
local clock = sc.clock("clock", period)

local exit_code = -1

local initiator = simple.Initiator("initiator")
local dut = simple.ExitCodeRegister("ExitCodeRegister")

initiator.target = dut.port
initiator.clock = clock

local function value_to_bytes_le(value)
  local bytes = {}
  for _ = 1, 4 do
    table.insert(bytes, value % 256)
    value = math.floor(value / 256)
  end
  return bytes
end

initiator:add_payload({ addr = 0, size = 4, data = value_to_bytes_le(exit_code) })

local success, result = pcall(function() sc.start(20 * period) end)

assert(not success, "Simulation should have stopped with Fatal Error")
assert(result ~= nil, "Expected fatal_error to propagate")
print("Caught error:", result)
print("Pass!")
