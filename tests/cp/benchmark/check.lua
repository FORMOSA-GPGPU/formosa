-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

local M = {}

function M.checker(system)
  local previous, started, stopped = 0, nil, nil
  local function word(address)
    local b = system:read_bytes(address, 4)
    assert(#b == 4, "incomplete benchmark mailbox read")
    return b[1] + b[2] * 256 + b[3] * 65536 + b[4] * 16777216
  end
  return function(_, elapsed)
    local phase = word(0x8000)
    if phase == previous then return nil end
    assert(
      phase == previous + 1,
      "invalid benchmark phase sequence: " .. previous .. " -> " .. phase
    )
    previous = phase
    if phase == 1 then
      started = elapsed
    elseif phase == 2 then
      stopped = elapsed
    elseif phase == 3 then
      local iterations, errors = word(0x8004), word(0x8008)
      assert(errors == 0, "benchmark validation failed: " .. errors .. " errors")
      assert(iterations > 0, "benchmark reported zero iterations")
      assert(started and stopped and stopped > started, "missing benchmark timing interval")
      return { iterations = iterations, cycles = stopped - started }
    end
  end
end

return M
