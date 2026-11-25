-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- A warp stopped during its first fetch loses that fetch. Restarting it at
-- the same PC, as the next work-group does, must fetch that PC again.
local bit = require("bit")

local entry = 0x100

local tester = simtix.FrontendTester("tester", {
  num_warps = 2,
  num_lanes = 4,
}, {
  num_subcores = 1,
  fetch_width = 4,
  decode_width = 1,
})

local period = sc.time(10, sc.time_unit.NS)
local clock = sc.clock("clock", period)
local mem = simple.Memory("mem", { size = 0x1000, latency = 20 })
tester.clock = clock
mem.clock = clock
tester.target = mem.port

-- Every word holds its own address.
for addr = 0, 0x1000 - 4, 4 do
  mem:write_bytes(addr, {
    bit.band(addr, 0xff),
    bit.band(bit.rshift(addr, 8), 0xff),
    0,
    0,
  })
end

sc.start(sc.ZERO_TIME)

tester:activate(0)
tester:restart(0, entry)
sc.start(2 * period)
tester:deactivate(0)
-- The first fetch completes while the warp is inactive.
sc.start(40 * period)

tester:activate(0)
tester:restart(0, entry)
local packet
for _ = 1, 100 do
  sc.start(period)
  packet = tester:issue(0)
  if packet then break end
end
assert(packet, "the restarted warp never received an instruction")
assert(
  packet.wpc == entry and packet.iword == entry,
  string.format("the restarted warp received 0x%x instead of 0x%x", packet.wpc, entry)
)
print("Pass: a restarted warp fetches its entry again")

local function counter(name)
  local record = tester.stats:dump_toml():match("%[tester%." .. name .. "%]([^%[]*)")
  return tonumber(assert(record):match("val = (%d+)"))
end

-- Warp 1 missed the first word. It must refetch while the tail is broadcasting.
local before = counter("fetch_requests")
tester:deactivate(0)
tester:activate(1)
tester:restart(1, entry)
sc.start(period)
assert(counter("fetch_requests") == before + 1, "the old group blocked a refetch of its first word")

-- The old group's next word must leave the replacement request in the filter.
local filtered = counter("num_fetches_filtered")
tester:deactivate(1)
tester:activate(0)
tester:restart(0, entry)
sc.start(period)
assert(
  counter("num_fetches_filtered") == filtered + 1,
  "the old group's tail erased the replacement request"
)
