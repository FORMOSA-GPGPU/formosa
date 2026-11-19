-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- Which writes dbg.AccessWatcher reports over the TLM paths that the
-- simulator's own initiators and targets never take: a target that completes
-- a write by returning TLM_COMPLETED for BEGIN_REQ, an initiator that
-- completes a response by returning TLM_COMPLETED for BEGIN_RESP, a write that
-- carries an annotation, b_transport, and a streaming write. The testers also
-- check that the watcher passes every call on unchanged.

local reports, streamed = {}, {}
local function record(addr, data, enables, annotation)
  reports[#reports + 1] = { addr = addr, data = data, enables = enables, annotation = annotation }
end
local function record_stream(addr, data) streamed[#streamed + 1] = { addr = addr, data = data } end

-- Puts a watcher between the endpoints of a tester.
local function path(name, param, watch, on_write)
  local tester = dbg.PassThroughTester(name .. "_tester", param)
  local watcher = dbg.AccessWatcher(name .. "_watcher", { watch = watch, on_write = on_write })
  tester.to = watcher.from
  watcher.to = tester.from
  return { tester = tester, watcher = watcher }
end

local watch = { { addr = 0x0, size = 0x100 }, { addr = 0x200, size = 0x10 } }
local completing = path(
  "completing",
  { mode = "complete", failing = { { addr = 0x200, size = 0x10 } } },
  watch,
  record
)
local responding =
  path("responding", { mode = "respond_later", response_latency_ns = 10 }, watch, record)
local delaying = path("delaying", { mode = "complete", added_delay_ns = 5 }, watch, record)
-- Watches only 0x44-0x47, so it tells a streaming write's 4-byte window from
-- the bytes its data length would span.
local streaming = path(
  "streaming",
  { mode = "complete" },
  { { addr = 0x44, size = 4 } },
  record_stream
)

-- Checks its response from inside the report of its first beat, while the
-- response is still crossing the watcher.
local early_reports, early_check = {}, nil
local early
early = path(
  "early",
  { mode = "respond_later", response_latency_ns = 10 },
  { { addr = 0x44, size = 4 } },
  function(_, data)
    early_reports[#early_reports + 1] = data
    if #early_reports == 1 then early_check = early.tester:check_responses() end
  end
)

sc.start(sc.ZERO_TIME)

local function check(what, result)
  assert(result.ok, what .. ": " .. result.reason)
  return result
end

-- A write completed by TLM_COMPLETED for BEGIN_REQ, with byte enables shorter
-- than the data and an annotation.
check(
  "masked write",
  completing.tester:write({
    addr = 0x10,
    data = { 1, 2, 3, 4 },
    byte_enables = { 0xff, 0 },
    annotation = "masked write",
  })
)

-- Neither a failed write nor a read is reported.
local failed = check("failed write", completing.tester:write({ addr = 0x200, data = { 5 } }))
assert(not failed.response_ok, "the failed write succeeded")
check("read", completing.tester:read({ addr = 0x20, data = { 0 } }))

-- A write responded with BEGIN_RESP that the initiator completes is reported
-- once.
local responded =
  check("responded write", responding.tester:write({ addr = 0x20, data = { 6, 7 } }))
assert(responded.status == "accepted", "the target did not answer later")
sc.start(sc.time(100, sc.time_unit.NS))
check("response", responding.tester:check_responses())

-- A write made with b_transport is reported.
local blocking =
  check("b_transport write", completing.tester:write_blocking({ addr = 0x30, data = { 8, 9 } }))
assert(blocking.response_ok, "the b_transport write failed")

-- A write completed with an annotated delay is reported.
local delayed = check("delayed write", delaying.tester:write({ addr = 0x40, data = { 10 } }))
assert(delayed.delay_ns == 5, "the target added no delay")

assert(#reports == 4, "expected exactly four reports, got " .. #reports)
local first = reports[1]
assert(
  first.addr == 0x10 and #first.data == 4 and first.data[4] == 4,
  "write completed for BEGIN_REQ was reported wrongly"
)
assert(
  #first.enables == 4
    and first.enables[1]
    and not first.enables[2]
    and first.enables[3]
    and not first.enables[4],
  "byte enables were not repeated over the data"
)
assert(first.annotation == "masked write", "the write's annotation was not reported")
local second = reports[2]
assert(
  second.addr == 0x20
    and #second.data == 2
    and second.data[2] == 7
    and second.enables == nil
    and second.annotation == nil,
  "write responded with BEGIN_RESP was reported wrongly"
)
assert(
  reports[3].addr == 0x30 and #reports[3].data == 2 and reports[3].data[2] == 9,
  "write made with b_transport was reported wrongly"
)
assert(
  reports[4].addr == 0x40 and reports[4].data[1] == 10,
  "write completed with a delay was reported wrongly"
)

-- A streaming write is watched over its 4-byte window and reported once per
-- beat at its address: the one at 0x40 never touches 0x44.
local beats = { 1, 2, 3, 4, 5, 6, 7, 8 }
check(
  "outside streaming write",
  streaming.tester:write({ addr = 0x40, data = beats, streaming_width = 4 })
)
check(
  "inside streaming write",
  streaming.tester:write({ addr = 0x44, data = beats, streaming_width = 4 })
)
assert(#streamed == 2, "a streaming write was not reported per beat within its window")
for beat, report in ipairs(streamed) do
  assert(
    report.addr == 0x44 and #report.data == 4 and report.data[1] == 4 * beat - 3,
    "a streaming write's beat was reported wrongly"
  )
end

-- A check made while a response is still crossing fails and leaves the
-- transaction in place, so the write's second beat still reads its own bytes.
check("early write", early.tester:write({ addr = 0x44, data = beats, streaming_width = 4 }))
sc.start(sc.time(100, sc.time_unit.NS))
assert(
  early_check and early_check.reason == "a response is still crossing the component",
  "a response was checked while still crossing"
)
assert(#early_reports == 2, "the early write was not reported per beat")
for beat, data in ipairs(early_reports) do
  assert(
    #data == 4 and data[1] == 4 * beat - 3 and data[4] == 4 * beat,
    "a beat reported after an early check was corrupted"
  )
end
check("early response", early.tester:check_responses())

print("Pass!")
