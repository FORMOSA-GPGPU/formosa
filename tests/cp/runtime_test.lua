-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

local System = require("cp.system")
local riscv_test = require("cp.riscv-test.check")
local elf = { entry = 0x80000000 }
workload = {
  ELF = function(path)
    assert(path == "test.elf")
    return elf
  end,
}

local function new_system(logs, load_success)
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
  function system:output() return logs[math.min(self.steps, #logs)] end
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

local function checker(manifest, expected_error)
  local path = os.tmpname()
  local file = assert(io.open(path, "w"))
  file:write(manifest)
  file:close()
  local ok, result = pcall(riscv_test.checker, path)
  os.remove(path)
  if expected_error then
    assert(not ok, "invalid manifest unexpectedly accepted")
    assert(tostring(result):find(expected_error, 1, true), tostring(result))
  else
    assert(ok, result)
    return result
  end
end

local manifest = "add\nsub\n"
local results = "add..OK\nsub..OK\n"
local done = "RISC-V test done\n"
local function check(logs, expected_error, load_success)
  return run(new_system(logs, load_success), checker(manifest), expected_error, 2)
end
local system = check({ "add..", "add..\27[32mOK\27[0m\r\nsub..OK\r\n" .. done })
assert(system.cycles == 4 and system.steps == 2, "runtime did not stop at completion")

check({ "add..ERR\n" .. done }, "reported ERR")
check({ results .. "Interrupted\n" .. done }, "unexpected trap")
check({ "add..OK\n" .. done }, "missing test result: sub")
check({ results .. "mul..OK\n" .. done }, "unexpected test result: mul")
check({ results .. "bad-name..OK\n" .. done }, "unexpected test result: bad-name")
check({ results .. "add..OK\n" .. done }, "duplicate test result: add")
check({ results .. done .. done }, "duplicate RISC-V test completion marker")
checker("", "empty RISC-V test manifest")
checker("add\nadd\n", "duplicate RISC-V test manifest entry: add")
check({ results .. done }, "failed to load CPU test ELF", false)

-- A complete set of OK lines is insufficient without program completion.
system = check({ results }, "timed out after 5 cycles")
assert(system.cycles == 5 and system.steps == 3, "runtime exceeded the cycle budget")
check({ results .. "RISC-V test done" }, "timed out")
check({ "" }, "timed out")

-- An unrelated program completes through memory, with no manifest or UART
-- protocol. Its result and final partial chunk must survive the shared runtime.
system = new_system({ "ERR is ordinary output for another program" })
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
run(new_system({ "" }), function() return false end, "checker reported failure")
run(
  new_system({ "" }),
  function() error("custom validation failed") end,
  "custom validation failed"
)
system = new_system({ "" })
function system:output() error("UART unavailable") end
run(system, function() error("original failure") end, "original failure")
print("cp.harness: program execution, result validation and timeout checks passed")
