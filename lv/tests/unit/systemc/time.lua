-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local t0 = sc.ZERO_TIME

for _ = 1, 100 do
  -- Randomly generate a step time
  local step = sc.time(math.random(), sc.time_unit.NS)

  -- Run the simulation for the step time
  sc.start(step)
  print(sc.time_stamp())

  assert(t0 + step == sc.time_stamp(), "Current time must be t0 + step time")

  -- Update t0
  t0 = t0 + step
end

print("Pass!")
