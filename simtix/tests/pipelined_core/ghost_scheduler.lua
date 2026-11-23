-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local bit = require("bit")

local NUM_WARPS = 4
local NUM_LANES = 4
local NUM_SUBCORES = 1
local DECODE_WIDTH = 4
local WID = 0

local function make_i(rd, rs1, imm)
  return bit.bor(bit.lshift(bit.band(imm, 0xfff), 20), bit.lshift(rs1, 15), bit.lshift(rd, 7), 0x13)
end

local function make_lui(rd, imm20)
  return bit.bor(bit.lshift(bit.band(imm20, 0xfffff), 12), bit.lshift(rd, 7), 0x37)
end

local param = {
  num_warps = NUM_WARPS,
  num_lanes = NUM_LANES,
}

local pipe_param = {
  num_subcores = NUM_SUBCORES,
  decode_width = DECODE_WIDTH,
}

local ghost_param = {
  num_isb_entries_per_warp = 4,
  num_itab_entries_per_warp = 2,
}

local tester = simtix.GhostSchedulerTester("tester", param, pipe_param, ghost_param, 0)
local period = sc.time(10, sc.time_unit.NS)
local edge_epsilon = sc.time(1, sc.time_unit.PS)
local clock = sc.clock("clock", period)
tester.clock = clock
tester:activate(WID)

sc.start(sc.ZERO_TIME)

local function step(cycles) sc.start(cycles * period + edge_epsilon) end

local function wait_until(predicate, description)
  for _ = 1, 32 do
    local value = predicate()
    if value then return value end
    step(1)
  end

  error("Timed out waiting for " .. description)
end

local function issue_tag(expected)
  wait_until(function()
    local packet = tester:peek(WID)
    return packet and packet.wpc == expected
  end, "issue candidate " .. expected)

  local packet = tester:issue(WID)
  assert(packet and packet.wpc == expected)
  return packet
end

local BLOCKER = make_i(31, 0, 1)
local OLDER_BLOCKED = make_i(5, 31, 0)
local YOUNGER_READY = make_lui(6, 1)

tester:scoreboard_issue(WID, BLOCKER)
tester:enqueue(WID, OLDER_BLOCKED, 0)
tester:enqueue(WID, YOUNGER_READY, 1)

local first = issue_tag(1)
assert(first.wpc == 1, "GhOST did not issue the younger ready instruction first")
tester:finish(first)

tester:scoreboard_finish(WID, BLOCKER)
local second = issue_tag(0)
assert(second.wpc == 0, "GhOST did not issue the older instruction after the hazard cleared")
tester:finish(second)

-- These three instructions enter the IsB in the same cycle. The middle
-- instruction depends on the blocked oldest instruction, while the youngest
-- is independent. This checks same-cycle IsB dependency tracking: without it,
-- the middle instruction would incorrectly be nominated first.
local SAME_CYCLE_OLDER_BLOCKED = make_i(5, 31, 0)
local SAME_CYCLE_DEPENDENT = make_i(6, 5, 0)
local SAME_CYCLE_READY = make_lui(7, 1)

tester:scoreboard_issue(WID, BLOCKER)
tester:enqueue(WID, SAME_CYCLE_OLDER_BLOCKED, 10)
tester:enqueue(WID, SAME_CYCLE_DEPENDENT, 11)
tester:enqueue(WID, SAME_CYCLE_READY, 12)

local independent = issue_tag(12)
tester:finish(independent)

tester:scoreboard_finish(WID, BLOCKER)
local older = issue_tag(10)
tester:finish(older)
local dependent = issue_tag(11)
tester:finish(dependent)

-- Ordered instructions must wait for older issue, but not older completion.
-- Once dispatched, they also hold back independent younger instructions until
-- the backend resolves them. No register dependency enforces either ordering.
for _, word in ipairs({
  0x00102073, -- csrrs x0, fflags, x0
  0x0ff0000f, -- fence iorw, iorw
  0x00000073, -- ecall
  0x0200002b, -- fsa.pri.reset
  0x0000502b, -- fsa.pri.raise
  0x0000302b, -- fsa.pri.lower
  0x0000102b, -- fsa.bar
}) do
  tester:scoreboard_issue(WID, BLOCKER)
  tester:enqueue(WID, OLDER_BLOCKED, 20)
  tester:enqueue(WID, word, 21)
  tester:enqueue(WID, YOUNGER_READY, 22)
  step(4)
  -- The in-order fallback may nominate the blocked oldest instruction; the
  -- backend checks its scoreboard before issuing it.
  local candidate = tester:peek(WID)
  assert(
    not candidate or candidate.wpc == 20,
    "Ordered instruction bypassed an unissued older instruction"
  )

  tester:scoreboard_finish(WID, BLOCKER)
  local pending_older = issue_tag(20)
  local ordered = issue_tag(21)
  step(4)
  assert(not tester:peek(WID), "Younger instruction bypassed unresolved ordered instruction")
  -- Backend execution notifications are not prefiltered by instruction class.
  -- A different instruction's ID must not release the pending dispatch block.
  tester:finish(pending_older)
  step(4)
  assert(not tester:peek(WID), "Older instruction incorrectly resolved the dispatch block")
  tester:finish(ordered)
  local younger = issue_tag(22)
  tester:finish(younger)
end

assert(tester.outstanding_packets == 0)

lv.info("GhOST scheduler out-of-order issue test passed")
