-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

-- Each binding failure gets a fresh SystemC simulation in its own test process.
local scenario = assert(({ ... })[1], "expected a binding scenario")
local clock = sc.clock("clock", sc.time(10, sc.time_unit.NS))
local cpu = cp.CommandProcessor("cpu", 0, { rsp_enable = false })
local ram = simple.Memory("ram", { size = 4096 })
local clint = simple.Clint("clint", 1)
local debugger = dbg.MemoryDebugger("debugger")
local timer = sc.signal("timer", false)
local software = sc.signal("software", false)
local spare = sc.signal("spare", false)

cpu.clock, ram.clock, clint.clock = clock, clock, clock
cpu.target = ram.port
debugger.target = clint.port

local function rejects(fn, expected)
  local ok, message = pcall(fn)
  assert(not ok, "invalid wiring unexpectedly succeeded")
  if expected then assert(tostring(message):find(expected, 1, true), tostring(message)) end
end

-- Both call forms must reject wrong types without consuming a binding slot.
for _, port in ipairs({ cpu.ext_int, clint.timer_irq[1] }) do
  rejects(function() port(nil) end, "no matching method overload")
  rejects(function() port:bind(false) end, "no matching method overload")
  rejects(function() port(cpu.sw_int) end, "no matching method overload")
end
rejects(function() sc.signal(false) end, "no matching constructor overload for sc.signal")
rejects(function() cpu.ext_int = timer end)
rejects(function() sc.BoolIn("input") end)
rejects(function() sc.BoolOut("output") end)

-- Ordinary array elements refer to real ports; retain an extracted port too.
local output = clint.timer_irq[1]
output(timer)
if scenario ~= "missing-output" then
  clint.msip_irq[1]:bind(scenario == "multiple-writers" and timer or software)
end
cpu.timer_int:bind(timer)
cpu.sw_int(software)
if scenario ~= "missing-input" then cpu.ext_int(timer) end

local expected
if scenario == "duplicate-input" then
  cpu.ext_int:bind(spare)
  expected = "2 binds exceeds maximum of 1 allowed"
elseif scenario == "duplicate-output" then
  output:bind(spare)
  expected = "2 binds exceeds maximum of 1 allowed"
elseif scenario == "missing-input" or scenario == "missing-output" then
  expected = "port not bound"
elseif scenario == "multiple-writers" then
  expected = "cannot have more than one driver"
else
  assert(scenario == "late", "unknown binding scenario")
end

if expected then
  rejects(function() sc.start(sc.ZERO_TIME) end, expected)
else
  sc.start(sc.ZERO_TIME)
  rejects(function() cpu.ext_int(spare) end, "simulation running")
  rejects(function() output:bind(spare) end, "simulation running")
  assert(not timer:read() and not software:read())
end
print("SystemC boolean ports: " .. scenario .. " rejected as expected")
