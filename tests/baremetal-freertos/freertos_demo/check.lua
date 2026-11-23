-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

return function(system)
  local output = system:output()
  assert(not output:find("failed!", 1, true), "FreeRTOS task creation failed")
  assert(not output:find("Scheduler ended", 1, true), "FreeRTOS scheduler returned")
  local previous, first_tick, switches = nil, nil, 0
  for task, tick in output:gmatch("Hello from Task ([12]) at tick (%d+)!\n") do
    tick = tonumber(tick)
    first_tick = first_tick or tick
    if previous and task ~= previous then switches = switches + 1 end
    previous = task
    -- Non-yielding tasks can alternate only through timer-driven preemption.
    if switches >= 3 and tick >= first_tick + 3 then return true end
  end
end
