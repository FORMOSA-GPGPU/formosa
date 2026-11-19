-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- An AccessWatcher must not change timing and must report exactly the writes
-- that overlap its watched ranges and that the target completes with OK.

local period = sc.time(10, sc.time_unit.NS)
local clock = sc.clock("clk", period)
local MEM_SIZE = 0x400
local WATCHED = { addr = 0x100, size = 0x10 }
-- Routed by the XBar but past the end of the memory, so writes fail.
local BEYOND = { addr = 0x500, size = 0x10 }

-- Two identical initiator -> XBar -> memory paths; only one has a watcher.
local function make_path(name, watcher)
  local initiator = simple.Initiator(name .. "_initiator")
  initiator.clock = clock
  local xbar = simple.XBar(name .. "_xbar", 1, { { addr = 0x1000, size = 2 * MEM_SIZE } })
  xbar.clock = clock
  local memory = simple.Memory(name .. "_memory", { size = MEM_SIZE, latency = 2 })
  memory.clock = clock
  initiator.target = xbar.core_side[1].port
  if watcher then
    xbar.mem_side[1].target = watcher.from
    watcher.to = memory.port
  else
    xbar.mem_side[1].target = memory.port
  end
  return { initiator = initiator, memory = memory }
end

local reports = {}
local watcher = dbg.AccessWatcher("watcher", {
  watch = { WATCHED, BEYOND },
  on_write = function(addr, data, enables)
    table.insert(reports, { addr = addr, data = data, enables = enables })
  end,
})

local plain = make_path("plain", nil)
local watched = make_path("watched", watcher)

local payloads = {
  { addr = 0x1000, data = { 1, 2, 3, 4 } }, -- write outside the ranges
  { addr = 0x1100, data = { 0xaa, 0xbb } }, -- write inside a range
  { addr = 0x1100, size = 2 }, -- read inside a range
  { addr = 0x10fc, data = { 5, 6, 7, 8, 9, 10 } }, -- write straddling a start
  { addr = 0x1110, data = { 0xcc } }, -- write right after a range
  { addr = 0x1500, data = { 0xdd } }, -- write inside a range that fails
  { addr = 0x1200, size = 8 }, -- read outside the ranges
}
for _, payload in ipairs(payloads) do
  plain.initiator:add_payload(payload)
  watched.initiator:add_payload(payload)
end

-- Both paths must complete each transaction in the same cycle.
for cycle = 1, 200 do
  sc.start(period)
  local expected = plain.initiator:completed_count()
  local actual = watched.initiator:completed_count()
  assert(
    expected == actual,
    string.format("cycle %d: %d completions without watcher, %d with", cycle, expected, actual)
  )
end
assert(plain.initiator:completed_count() == #payloads, "transactions did not complete")

-- Reads, writes outside the ranges and failed writes are not reported;
-- addresses are as seen on the link, after the XBar removes its base.
assert(#reports == 2, "expected 2 reports, got " .. #reports)
assert(reports[1].addr == 0x100 and #reports[1].data == 2)
assert(reports[1].data[1] == 0xaa and reports[1].data[2] == 0xbb)
assert(reports[1].enables == nil)
assert(reports[2].addr == 0xfc and #reports[2].data == 6 and reports[2].data[6] == 10)

-- The watcher passes writes on unchanged.
local bytes = watched.memory:read_bytes(0xfc, 8)
local expected = { 5, 6, 7, 8, 9, 10, 0, 0 }
for i = 1, #expected do
  assert(bytes[i] == expected[i], string.format("byte %d: %d != %d", i, bytes[i], expected[i]))
end

print("Pass!")
