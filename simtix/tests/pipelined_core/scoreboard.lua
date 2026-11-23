-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local bit = require("bit")
local argparse = require("argparse")

-- Parameters
local parser = argparse("scoreboard.lua")
parser:option("--num-warps", "Number of warps"):default(32):convert(tonumber)
parser:option("--num-subcores", "Number of subcores"):default(4):convert(tonumber)
parser:option("--seed", "Random seed"):default(os.time()):convert(tonumber)

local args = parser:parse({ ... })

local NUM_WARPS = args.num_warps
local NUM_SUBCORES = args.num_subcores

local param = {
  num_warps = NUM_WARPS,
  num_lanes = 4, -- doesn't matter in this test
}

local pipe_param = {
  num_subcores = NUM_SUBCORES,
}

local tester = simtix.ScoreboardTester(param, pipe_param)
local kNullReg = 0xff

-- Helper to construct R-type instruction word
local function make_r_type(rd, rs1, rs2)
  local iword = bit.bor(bit.lshift(rs2, 20), bit.lshift(rs1, 15), bit.lshift(rd, 7), 0x33)
  return iword
end

-- Helper to construct I-type instruction word
local function make_i_type(rd, rs1, imm)
  local iword = bit.bor(bit.lshift(imm, 20), bit.lshift(rs1, 15), bit.lshift(rd, 7), 0x13)
  return iword
end

-- Helper to construct B-type (branch) instruction word
local function make_beq(rs1, rs2)
  local iword = bit.bor(bit.lshift(rs2, 20), bit.lshift(rs1, 15), 0x63)
  return iword
end

-- Helper to construct R4-type instruction word
local function make_r4_type(rd, rs1, rs2, rs3)
  local iword =
    bit.bor(bit.lshift(rs3, 27), bit.lshift(rs2, 20), bit.lshift(rs1, 15), bit.lshift(rd, 7), 0x43)
  return iword
end

-- Helper to construct LW instruction word
local function make_lw(rd, rs1, imm)
  local iword =
    bit.bor(bit.lshift(imm, 20), bit.lshift(rs1, 15), bit.lshift(2, 12), bit.lshift(rd, 7), 0x03)
  return iword
end

-- Helper to construct SW instruction word
local function make_sw(rs1, rs2, imm)
  local imm_hi = bit.rshift(bit.band(imm, 0xFE0), 5)
  local imm_lo = bit.band(imm, 0x1F)
  local iword = bit.bor(
    bit.lshift(imm_hi, 25),
    bit.lshift(rs2, 20),
    bit.lshift(rs1, 15),
    bit.lshift(2, 12),
    bit.lshift(imm_lo, 7),
    0x23
  )
  return iword
end

-- Generate a random instruction
local function random_instr()
  local r = math.random(1, 6)
  local iword
  local memory_kind = "none"
  if r == 1 then
    iword = make_r_type(math.random(0, 31), math.random(0, 31), math.random(0, 31))
  elseif r == 2 then
    iword = make_i_type(math.random(0, 31), math.random(0, 31), math.random(0, 100))
  elseif r == 3 then
    iword = make_beq(math.random(0, 31), math.random(0, 31))
  elseif r == 4 then
    iword =
      make_r4_type(math.random(0, 31), math.random(0, 31), math.random(0, 31), math.random(0, 31))
  elseif r == 5 then
    iword = make_lw(math.random(0, 31), math.random(0, 31), math.random(0, 100))
    memory_kind = "load"
  elseif r == 6 then
    iword = make_sw(math.random(0, 31), math.random(0, 31), math.random(0, 100))
    memory_kind = "store"
  else
    iword = math.random(0, 0x7FFFFFFF)
  end
  return simtix.decode(iword), memory_kind
end

local function is_valid(rid) return rid ~= kNullReg and rid ~= 0 end

local function check_hw_can_issue(instr1, memory_kind1, instr2, memory_kind2)
  if instr1.is_cti then return false, "instr1 is CTI", 3 end

  local function is_being_written(rid)
    return is_valid(rid) and is_valid(instr1.rd) and rid == instr1.rd
  end

  if
    is_being_written(instr2.rs1)
    or is_being_written(instr2.rs2)
    or is_being_written(instr2.rs3)
    or is_being_written(instr2.rd)
  then
    return false, "data dependencies", 2
  end

  -- The in-order operand collector keeps memory order, so memory pairs need no
  -- scoreboard stall.
  if memory_kind1 ~= "none" and memory_kind2 ~= "none" then
    return true, "memory pair, no stall", 4
  end

  return true, "same warp, but no dependencies", 1
end

-- Perform the dependency check property
local function check_dependencies(wid1, instr1, memory_kind1, wid2, instr2, memory_kind2)
  -- Initial state check: first instruction should be able to issue
  if not tester:can_issue(wid1, instr1) then
    error("Instruction 1 should be able to issue at the beginning of the test")
  end

  tester:issue(wid1, instr1)

  local can_issue = tester:can_issue(wid2, instr2)
  local golden_can_issue = true
  local reason = ""
  local type = -1

  -- Dependencies only exist within the same warp (wid)
  if wid1 == wid2 then
    golden_can_issue, reason, type = check_hw_can_issue(instr1, memory_kind1, instr2, memory_kind2)
  else
    type = 0
    reason = "different warp, no dependency"
  end

  lv.debug(
    string.format(
      "w%d: %s -> w%d: %s, can_issue = %s, %s",
      wid1,
      instr1,
      wid2,
      instr2,
      tostring(can_issue),
      reason
    )
  )

  if can_issue ~= golden_can_issue then
    error(
      string.format(
        "Dependency check failed!\n"
          .. "Instr1: wid=%d, rd=%d, is_cti=%s\n"
          .. "Instr2: wid=%d, rs1=%d, rs2=%d, rs3=%d, rd=%d\n"
          .. "Expected can_issue: %s, Got: %s",
        wid1,
        instr1.rd,
        tostring(instr1.is_cti),
        wid2,
        instr2.rs1,
        instr2.rs2,
        instr2.rs3,
        instr2.rd,
        tostring(golden_can_issue),
        tostring(can_issue)
      )
    )
  end

  -- Commit first instruction. Second one should then be able to issue.
  tester:commit(wid1, instr1)

  if not tester:can_issue(wid2, instr2) then
    error("Instruction 2 should be able to issue after Instruction 1 commits")
  end

  -- Clean up by issuing and committing second instruction
  tester:issue(wid2, instr2)
  tester:commit(wid2, instr2)

  return type
end

-- Execution-state operations are not CTIs, but still block subsequent issue.
-- fsa.pri.reset / raise / lower and fsa.bar encodings.
for _, word in ipairs({ 0x0200002b, 0x502b, 0x302b, 0x102b }) do
  local control = simtix.decode(word)
  local next_instr = simtix.decode(make_r_type(3, 4, 5))
  assert(not control.is_cti, "Execution-state operation must not be classified as CTI")
  assert(tester:can_issue(0, control))
  tester:issue(0, control)
  assert(
    not tester:can_issue(0, next_instr),
    "Execution-state operation must block same-warp issue"
  )
  if NUM_WARPS > 1 then
    assert(tester:can_issue(1, next_instr), "Other warps must remain issuable")
  end
  tester:commit(0, control)
  assert(tester:can_issue(0, next_instr), "Commit must release execution-state dependency")
end

-- Run random tests
math.randomseed(args.seed)
lv.info("Starting Scoreboard constrained random tests...")

local counts = { [0] = 0, [1] = 0, [2] = 0, [3] = 0, [4] = 0 }
local total = 0

while true do
  local wid1 = math.random(0, NUM_WARPS - 1)
  local wid2 = math.random(0, NUM_WARPS - 1)

  -- Bias the generation slightly to hit the harder cases (same warp)
  if math.random() < 0.3 then wid2 = wid1 end

  local instr1, memory_kind1 = random_instr()
  local instr2, memory_kind2 = random_instr()

  local type = check_dependencies(wid1, instr1, memory_kind1, wid2, instr2, memory_kind2)
  counts[type] = counts[type] + 1
  total = total + 1

  local finished = true
  for i = 0, 4 do
    if counts[i] < 100 then
      finished = false
      break
    end
  end

  if finished then break end
end

-- Independent load, store and atomic may overlap: the in-order operand collector
-- delivers them to the LSU in issue order.
do
  local wid = 0
  local load1 = simtix.decode(make_lw(1, 2, 0))
  local store1 = simtix.decode(make_sw(5, 6, 8))
  local atomic1 =
    simtix.decode(bit.bor(bit.lshift(6, 20), bit.lshift(5, 15), bit.lshift(3, 7), 0x202f)) -- amoadd.w x3, x6, (x5)

  tester:issue(wid, load1)
  assert(tester:can_issue(wid, store1), "Store must not wait for an in-flight load")
  tester:issue(wid, store1)
  assert(tester:can_issue(wid, atomic1), "Atomic must not wait for in-flight memory")
  tester:issue(wid, atomic1)
  tester:commit(wid, load1)
  tester:commit(wid, store1)
  tester:commit(wid, atomic1)
end

-- A writer may issue while an earlier reader of its destination is still in the
-- collector (no WAR stall).
do
  local wid = 0
  local reader = simtix.decode(make_r_type(1, 5, 6))
  local writer = simtix.decode(make_r_type(5, 7, 8))
  tester:issue(wid, reader)
  assert(tester:can_issue(wid, writer), "WAR must not stall issue")
  tester:commit(wid, reader)
end

-- More loads than any per-warp limit may be in flight.
do
  local wid = 0
  local loads = {}
  for rd = 1, 16 do
    local load = simtix.decode(make_lw(rd, 31, 0))
    assert(tester:can_issue(wid, load), "Independent loads must not be limited per warp")
    tester:issue(wid, load)
    loads[#loads + 1] = load
  end
  for _, load in ipairs(loads) do
    tester:commit(wid, load)
  end
end

lv.println("\nCoverage report:")
lv.println(string.format("  Type 0 (Different warp):      %d", counts[0]))
lv.println(string.format("  Type 1 (Same warp, no dep):   %d", counts[1]))
lv.println(string.format("  Type 2 (Same warp, data dep): %d", counts[2]))
lv.println(string.format("  Type 3 (Same warp, CTI dep):  %d", counts[3]))
lv.println(string.format("  Type 4 (Same warp, Mem pair): %d", counts[4]))
lv.println(string.format("Total tests run: %d", total))

lv.info("All tests passed!")
