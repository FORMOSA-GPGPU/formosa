-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

local benchmark = require("cp.benchmark.check")

local function checker(iterations, errors)
  local phase = 0
  local system = {}
  function system:read_bytes(addr, size)
    assert(size == 4)
    local value = assert(({ [0x8000] = phase, [0x8004] = iterations, [0x8008] = errors })[addr])
    local bytes = {}
    for i = 1, 4 do
      bytes[i] = value % 256
      value = math.floor(value / 256)
    end
    return bytes
  end
  local check = benchmark.checker(system)
  return function(next_phase, elapsed)
    phase = next_phase
    return check(system, elapsed)
  end
end

-- Repeated observations must not move either boundary. Initialization and
-- validation are outside the interval, and all four iteration bytes survive.
local check = checker(0x12345678, 0)
assert(check(0, 2) == nil)
assert(check(1, 3) == nil)
assert(check(1, 5) == nil)
assert(check(2, 6) == nil)
assert(check(2, 7) == nil)
local result = check(3, 8)
assert(result.iterations == 0x12345678, "incorrect benchmark iteration count")
assert(result.cycles == 3, "benchmark timing includes setup or validation")

local function reject(phases, iterations, errors, expected_error)
  local validate = checker(iterations, errors)
  local ok, message = pcall(function()
    for elapsed, phase in ipairs(phases) do
      validate(phase, elapsed)
    end
  end)
  assert(not ok, "invalid benchmark result unexpectedly passed")
  assert(tostring(message):find(expected_error, 1, true), tostring(message))
end
reject({ 1, 2, 3 }, 10, 1, "benchmark validation failed")
reject({ 1, 2, 3 }, 0, 0, "zero iterations")
reject({ 2 }, 10, 0, "invalid benchmark phase sequence")
reject({ 1, 3 }, 10, 0, "invalid benchmark phase sequence")
reject({ 1, 0 }, 10, 0, "invalid benchmark phase sequence")

-- A stop marker alone must remain pending so the runtime eventually times out.
check = checker(10, 0)
assert(check(1, 1) == nil)
assert(check(2, 2) == nil)
assert(check(2, 8) == nil, "benchmark completed before validation")
print("cp.benchmark.check: timing and result validation checks passed")
