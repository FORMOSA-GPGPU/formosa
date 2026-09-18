-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local initiator = simple.Initiator("initiator")
local target = simple.DummyTarget("target", { verbose = true })
local clock = sc.clock("clock", sc.time(1, sc.time_unit.NS))

initiator.target = target.port
initiator.clock = clock
local payloads = {
  { addr = 0x1234, data = { 1, 2, 3, 4 } },
  { addr = 0x5677, data = { 56, 78 } },
  { addr = 0xdeadbeef, data = { 0xca, 0xfe, 0xaa, 0xcc } },
}

for i = 1, #payloads do
  initiator:add_payload(payloads[i])
end

sc.start(sc.time(2, sc.time_unit.NS))
assert(initiator:completed_count() == #payloads)
