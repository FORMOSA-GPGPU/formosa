-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local gu = require("util.gutil")
local bit = require("bit")
gu.log("green", "SMTransactor test")
trace.settings.streaming = true
trace.settings.file_write_period_ms = 1000
trace.settings.flush_period_ms = 250
trace.settings.flush_period_sc_time = sc.time(100, sc.time_unit.US)
trace.settings.buffer_size_kb = 16384
trace.settings.output_prefix = "SMTransactor"

local pf_tb = trace.Track("tb", false)

local initiator = simple.Initiator("initiator")
local smt = formosa.SMTransactor("SMTransactor", { limit = 4, delay = false, enable_trace = true })

local period = sc.time(10, sc.time_unit.NS)
local clock = sc.clock("clock", period)

gu.log("yellow", "Bind ports")
initiator.clock = clock
smt.clock = clock

initiator.target = smt.slave_port

local function write_cmd(addr, data)
  local dtmp = {}
  for _ = 1, 8 do
    local byte = bit.band(data, 0xff)
    table.insert(dtmp, byte)
    data = bit.arshift(data, 8)
  end
  return { addr = addr, data = dtmp }
end

gu.log("yellow", "Prepare comamnds")
-- Test only enq/deq
-- Write 4 and the enq should block
-- Then read 1 and the enq should free
-- Then read the rest and the number should match

local cmds = {}
-- Write 4
table.insert(cmds, write_cmd(0x0, 1))
table.insert(cmds, write_cmd(0x0, 1))
table.insert(cmds, write_cmd(0x0, 1))
table.insert(cmds, write_cmd(0x0, 1))
-- and the enq should block
table.insert(cmds, { poll = true, addr = 0x0, value = 1 })
for _, t in ipairs(cmds) do
  initiator:add_payload(t)
end
trace.event_instant(pf_tb, "tb", "Start first part")
gu.log("yellow", "Start first part")
sc.start()

cmds = {}
-- Then read 1
table.insert(cmds, write_cmd(0x20, 0))
-- and the enq should free
table.insert(cmds, { poll = true, addr = 0x0, value = 0 })
for _, t in ipairs(cmds) do
  initiator:add_payload(t)
end
trace.event_instant(pf_tb, "tb", "Start second part")
gu.log("yellow", "Start second part")
sc.start()

cmds = {}
-- Then read the rest
table.insert(cmds, write_cmd(0x20, 0))
table.insert(cmds, write_cmd(0x20, 0))
table.insert(cmds, write_cmd(0x20, 0))
-- And the number should match
table.insert(cmds, { poll = true, addr = 0x20, value = 0 })
for _, t in ipairs(cmds) do
  initiator:add_payload(t)
end
trace.event_instant(pf_tb, "tb", "Start third part")
gu.log("yellow", "Start third part")
sc.start()

gu.log("green", "Pass")
