-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

local System = require("cp.system")
local elf = { entry = 0x80000000 }
workload = {
  ELF = function(path)
    assert(path == "test.elf")
    return elf
  end,
}

local function new_system(load_success)
  local system = { cycles = 0, steps = 0, closes = 0 }
  function system:load_elf(value)
    assert(value == elf)
    self.loaded = true
    return load_success ~= false
  end
  function system:boot(entry)
    assert(self.loaded and entry == elf.entry)
    self.booted = true
  end
  function system:step(cycles)
    assert(self.booted)
    self.cycles = self.cycles + cycles
    self.steps = self.steps + 1
  end
  function system:output() return "ERR is ordinary output for a program" end
  function system:close() self.closes = self.closes + 1 end
  return system
end

local function run(system, check, expected_error, expected_result)
  local ok, result = pcall(System.run, system, "test.elf", check, {
    max_cycles = 5,
    chunk_cycles = 2,
  })
  assert(system.closes == 1, "runtime did not close its system exactly once")
  if expected_error then
    assert(not ok, "invalid test run unexpectedly passed")
    assert(tostring(result):find(expected_error, 1, true), tostring(result))
  else
    assert(ok, result)
    assert(result == expected_result, "wrong program result")
  end
  return system
end

local system = run(new_system(), function(_, elapsed)
  if elapsed == 4 then return 42 end
end, nil, 42)
assert(system.cycles == 4 and system.steps == 2, "runtime did not stop at completion")
run(
  new_system(false),
  function() error("checker ran before loading") end,
  "failed to load CPU test ELF"
)
system = run(new_system(), function() return nil end, "timed out after 5 cycles")
assert(system.cycles == 5 and system.steps == 3, "runtime exceeded the cycle budget")

-- An arbitrary program completes through memory; its result and the final
-- partial chunk must survive the shared runtime without an output protocol.
system = new_system()
function system:read_bytes(addr, size)
  assert(addr == 0x100 and size == 1)
  return { self.cycles == 5 and 42 or 0 }
end
local result = {}
run(system, function(cpu, elapsed)
  assert(elapsed == cpu.cycles)
  if cpu:read_bytes(0x100, 1)[1] == 42 then return result end
end, nil, result)
assert(system.cycles == 5 and system.steps == 3)
run(new_system(), function() return false end, "checker reported failure")
run(new_system(), function() error("custom validation failed") end, "custom validation failed")
system = new_system()
function system:output() error("UART unavailable") end
run(system, function() error("original failure") end, "original failure")

-- Reset belongs to the platform and follows its clock, including non-10 ns
-- clocks. Observe the actual signal on rising edges through System:boot.
local period = sc.time(7, sc.time_unit.NS)
local clock = sc.clock("reset_clock", period)
local reset_n = sc.signal("reset_n")
reset_n:write(true)
local booted, reset_edges = false, 0
local observer = dbg.TickAgent("reset_observer", function()
  if booted then
    assert(not reset_n:read(), "reset released before the reset interval ended")
    reset_edges = reset_edges + 1
  end
end)
observer.clock = clock
sc.start(sc.time(1, sc.time_unit.NS))
local start = sc.time_stamp()

-- A core that ignores reset must not execute outside the run's cycle budget.
local without_reset_booted = false
System.boot({
  _period = period,
  _reset_n = reset_n,
  step = System.step,
  core = {
    has_reset = false,
    boot = function(_, entry)
      assert(entry == 0x10000)
      without_reset_booted = true
    end,
  },
}, 0x10000)
assert(without_reset_booted, "entry was not prepared for the core without reset")
assert(sc.time_stamp() == start, "boot advanced a core without reset support")

System.boot({
  _period = period,
  _reset_n = reset_n,
  step = System.step,
  core = {
    has_reset = true,
    boot = function(_, entry)
      assert(entry == 0x10000)
      booted = true
    end,
  },
}, 0x10000)
assert(sc.time_stamp() - start == period * require("cp.platform").reset_cycles)
assert(reset_edges == require("cp.platform").reset_cycles, "incorrect reset pulse length")
sc.start(sc.time(0, sc.time_unit.NS))
assert(reset_n:read(), "reset was not released after boot")
print("cp.harness: execution, cycle budget, results, cleanup and reset checks passed")
