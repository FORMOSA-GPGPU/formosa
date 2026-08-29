-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local bit = require("bit")
local argparse = require("argparse")

-- Parameters
local parser = argparse("frontend.lua")
parser
  :option("--total-issued", "Total number of instructions to issue per warp")
  :default(1000)
  :convert(tonumber)
parser:option("--num-warps", "Number of warps"):default(32):convert(tonumber)
parser:option("--mem-size", "Memory size in bytes"):default(0x100000):convert(tonumber)
parser:option("--num-subcores", "Number of subcores"):default(2):convert(tonumber)
parser:option("--fetch-width", "Fetch width"):default(2):convert(tonumber)
parser:option("--decode-width", "Decode width"):default(2):convert(tonumber)
parser
  :option("--num-fetch-filter-entries", "Number of fetch filter entries")
  :default(8)
  :convert(tonumber)
parser:flag("--enable-iwis", "Enable IWIS"):default(false)
parser:option("--konata-trace-out", "Konata trace output file"):default("frontend_tester.log")

local args = parser:parse({ ... })

local TOTAL_ISSUED = args.total_issued
local NUM_WARPS = args.num_warps
local MEM_SIZE = args.mem_size
local NUM_SUBCORES = args.num_subcores
local FETCH_WIDTH = args.fetch_width
local DECODE_WIDTH = args.decode_width
local NUM_FETCH_FILTER_ENTRIES = args.num_fetch_filter_entries
local ENABLE_IWIS = args.enable_iwis
local KONATA_TRACE_OUT = args.konata_trace_out

local NUM_WARPS_PER_SUBCORE = NUM_WARPS / NUM_SUBCORES

local param = {
  num_warps = NUM_WARPS,
  num_lanes = 4, -- Doesn't matter in this test
}

local pipe_param = {
  fetch_width = FETCH_WIDTH,
  decode_width = DECODE_WIDTH,
  num_subcores = NUM_SUBCORES,
  num_fetch_filter_entries = NUM_FETCH_FILTER_ENTRIES,
  enable_iwis = ENABLE_IWIS,
  konata_trace_out = KONATA_TRACE_OUT,
}

local tester = simtix.FrontendTester("tester", param, pipe_param)

local period = sc.time(10, sc.time_unit.NS)
local clock = sc.clock("clock", period)
local mem = simple.Memory("mem", { size = MEM_SIZE })

tester.clock = clock
mem.clock = clock
tester.target = mem.port

sc.start(sc.ZERO_TIME)

local golden_wpc = {}
local issued_count = {}

for i = 0, NUM_WARPS - 1 do
  golden_wpc[i] = 0
  issued_count[i] = 0
  tester:activate(i)
  tester:redirect(i, golden_wpc[i])
end

-- Used for issuing policy
local prioritized_local_wid = {}
for i = 0, NUM_SUBCORES - 1 do
  prioritized_local_wid[i] = 0
end

-- Statistics for issue counts
local issue_stats = {}
for i = 0, NUM_SUBCORES do
  issue_stats[i] = 0
end
local total_cycles = 0

local function all_done()
  for i = 0, NUM_WARPS - 1 do
    if issued_count[i] < TOTAL_ISSUED then return false end
  end
  return true
end

-- Seed for reproducibility
math.randomseed(42)

-- Fill the memory so that mem[addr] == addr for every word
for addr = 0, MEM_SIZE - 4, 4 do
  local bytes = {}
  for i = 0, 3 do
    local b = bit.band(bit.arshift(addr, i * 8), 0xff)
    table.insert(bytes, b)
  end
  mem:write_bytes(addr, bytes)
end

while not all_done() do
  local issues_this_cycle = 0
  -- Each subcore tries to issue 1 instruction in round-robin fashion
  for s = 0, NUM_SUBCORES - 1 do
    -- 5% chance to simulate a backend stall for this subcore
    if math.random() >= 0.05 then
      for w = 0, NUM_WARPS_PER_SUBCORE - 1 do
        local local_wid = (prioritized_local_wid[s] + w) % NUM_WARPS_PER_SUBCORE
        local wid = local_wid * NUM_SUBCORES + s

        local packet = tester:issue(wid)
        if packet and issued_count[wid] < TOTAL_ISSUED then
          issues_this_cycle = issues_this_cycle + 1
          lv.debug(
            string.format("Issuing w%d, wpc = 0x%x, iword = 0x%x", wid, packet.wpc, packet.iword)
          )
          -- Check issued PC against golden value
          assert(
            packet.wpc == golden_wpc[wid],
            string.format(
              "Mismatch PC for warp %d: expected 0x%x, got 0x%x",
              wid,
              golden_wpc[wid],
              packet.wpc
            )
          )

          -- Check the iword of the instruction, should match wpc
          assert(
            packet.iword == packet.wpc,
            string.format(
              "Mismatch iword for warp %d: expected 0x%x, got 0x%x",
              wid,
              packet.wpc,
              packet.iword
            )
          )

          -- Check the wid of the instruction, should match
          assert(
            packet.wid == wid,
            string.format("Mismatch wid for warp %d: expected %d, got %d", wid, wid, packet.wid)
          )

          issued_count[wid] = issued_count[wid] + 1
          -- 10% chance to simulate a jump instruction
          if math.random() < 0.1 or golden_wpc[wid] == MEM_SIZE - 4 then
            local jump_pc = math.random(0, (MEM_SIZE / 4) - 1) * 4
            lv.debug(string.format("Redirect w%d from 0x%x to 0x%x", wid, golden_wpc[wid], jump_pc))
            tester:redirect(wid, jump_pc)
            golden_wpc[wid] = jump_pc
          else
            -- Update golden value for next instruction
            golden_wpc[wid] = golden_wpc[wid] + 4
          end

          -- Move to the next warp in the subcore's round-robin queue after a successful issue
          -- to maintain balanced progress across all warps.
          prioritized_local_wid[s] = local_wid % NUM_WARPS_PER_SUBCORE
          if issued_count[wid] >= TOTAL_ISSUED then tester:deactivate(wid) end

          break
        end
      end
    end

    issue_stats[issues_this_cycle] = issue_stats[issues_this_cycle] + 1
    total_cycles = total_cycles + 1

    -- Advance simulation
    sc.start(period)
  end
end

sc.start(10 * period)
lv.info("Finished: all warps issued " .. TOTAL_ISSUED .. " instructions.")

lv.println("\nIssue Statistics:")
for i = 0, NUM_SUBCORES do
  local ratio = (issue_stats[i] / total_cycles) * 100
  lv.println(string.format("%d issues: %d cycles (%.2f%%)", i, issue_stats[i], ratio))
end

lv.println("\nFrontend Statistics:")
lv.println(tostring(tester.stats))
