-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- dbg.Monitor and dbg.AccessWatcher pass every transport call on through
-- dbg::PassThrough. Each sits between the endpoints of a dbg.PassThroughTester
-- on every path its target can answer on, and the tester checks that calls,
-- statuses, phases and annotated delays cross the component unchanged, that no
-- call or return moves a transaction's effective time back, that b_transport
-- takes no simulated time, and that a DMI request is refused before it reaches
-- the target. The write's delay exceeds the response latency, so a response
-- that ignored it would come before the request's effective time.

local components = {
  { name = "monitor", make = function(name) return dbg.Monitor(name) end },
  {
    name = "access_watcher",
    make = function(name)
      return dbg.AccessWatcher(name, {
        watch = { { addr = 0, size = 0x1000 } },
        on_write = function() end,
      })
    end,
  },
}

local paths = {
  {
    mode = "complete",
    status = "completed",
    param = { added_delay_ns = 5, failing = { { addr = 0x200, size = 0x10 } } },
  },
  { mode = "update", status = "updated", param = { added_delay_ns = 4 } },
  { mode = "respond_later", status = "accepted", param = { response_latency_ns = 10 } },
}

local cases = {}
for _, component in ipairs(components) do
  for _, path in ipairs(paths) do
    local name = component.name .. "_" .. path.mode
    local param = { mode = path.mode }
    for key, value in pairs(path.param) do
      param[key] = value
    end
    local tester = dbg.PassThroughTester(name .. "_tester", param)
    local dut = component.make(name)
    tester.to = dut.from
    dut.to = tester.from
    cases[#cases + 1] = { name = name, path = path, tester = tester, dut = dut }
  end
end

sc.start(sc.ZERO_TIME)

local function check(case, what, result)
  assert(result.ok, string.format("%s, %s: %s", case.name, what, result.reason))
  return result
end

-- Lets a later response arrive, then checks it crossed the component.
local function settle(case, what)
  sc.start(sc.time(40, sc.time_unit.NS))
  check(case, what .. " response", case.tester:check_responses())
end

for _, case in ipairs(cases) do
  local tester = case.tester
  local write =
    check(case, "write", tester:write({ addr = 0x10, data = { 1, 2, 3, 4 }, delay_ns = 15 }))
  assert(write.status == case.path.status, case.name .. ": write took another path")
  settle(case, "write")
  check(case, "read", tester:read({ addr = 0x20, data = { 0 } }))
  settle(case, "read")
  if case.path.mode == "complete" then
    local failed = check(case, "failed write", tester:write({ addr = 0x200, data = { 5 } }))
    assert(not failed.response_ok, case.name .. ": the failed write succeeded")
  end
  local blocking = check(
    case,
    "b_transport",
    tester:write_blocking({ addr = 0x30, data = { 8, 9 }, delay_ns = 7 })
  )
  assert(blocking.response_ok, case.name .. ": the b_transport write failed")
  check(case, "DMI", tester:request_dmi({ addr = 0x30 }))
  check(case, "debug", tester:debug({ addr = 0x40, data = { 0, 0 } }))
end

print("Pass!")
