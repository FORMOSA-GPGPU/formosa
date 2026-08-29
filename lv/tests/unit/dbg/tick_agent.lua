-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

for i = 1, 10 do
  do
    local period = sc.time(10, sc.time_unit.NS)
    local clock = sc.clock("clock", period)

    local tick_agent = dbg.TickAgent("tick_agent", function()
      print(i, sc.time_stamp())
      -- Pause after starting for `i` cycles.
      if sc.time_stamp() == i * period then sc.pause() end
    end)
    tick_agent.clock = clock
    sc.start()

    assert(sc.time_stamp() == i * period)
    print(sc.time_stamp(), "==", i * period)
  end
  sc.reset()
end
print("Pass!")
