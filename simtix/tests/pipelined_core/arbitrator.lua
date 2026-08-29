-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local argparse = require("argparse")

-- Parameters
local parser = argparse("scoreboard.lua")
parser
  :option("--total-issued", "Total number of instructions to issue per warp")
  :default(1000)
  :convert(tonumber)
parser:option("--num-warps", "Number of warps"):default(32):convert(tonumber)
parser:option("--num-lanes", "Number of lanes"):default(32):convert(tonumber)
parser:option("--num-subcores", "Number of subcores"):default(4):convert(tonumber)
parser
  :option("--num-read-collect-units", "Number of read collect units")
  :default(2)
  :convert(tonumber)
parser
  :option("--num-write-collect-units", "Number of write collect units")
  :default(1)
  :convert(tonumber)
parser:option("--num-regfile-banks", "Number of register file banks"):default(2):convert(tonumber)
parser
  :option("--num-shared-ports", "Number of shared ports (if 0, use separate read/write ports)")
  :default(0)
  :convert(tonumber)
parser:option("--num-read-ports", "Number of read ports"):default(2):convert(tonumber)
parser:option("--num-write-ports", "Number of write ports"):default(1):convert(tonumber)
parser:option("--arbitrator", "The arbitrator to test"):default("simple")
parser:option("--arbitrator-config", "The arbitrator specific config to test"):default("Baseline")
parser:option("--seed", "Random seed"):default(os.time()):convert(tonumber)

local args = parser:parse({ ... })

local NUM_WARPS = args.num_warps
local NUM_LANES = args.num_lanes
local NUM_SUBCORES = args.num_subcores
local NUM_RCUS = args.num_read_collect_units
local NUM_WCUS = args.num_write_collect_units
local NUM_REGFILE_BANKS = args.num_regfile_banks
local NUM_SHARED_PORTS = args.num_shared_ports
local NUM_READ_PORTS = args.num_read_ports
local NUM_WRITE_PORTS = args.num_write_ports
local ARBITRATOR_CONFIG = args.arbitrator_config

local param = {
  num_warps = NUM_WARPS,
  num_lanes = NUM_LANES,
}

local pipe_param = {
  num_subcores = NUM_SUBCORES,
  konata_trace_out = "arbitrator.log",
}

local clock = sc.clock("clock", 10, sc.time_unit.NS)
local tester = simtix.ArbitratorTester("tester", param, pipe_param)
for i = 0, NUM_SUBCORES - 1 do
  tester:arbitrator_init(i, function(name)
    if args.arbitrator == "simple" then
      return simtix.SimpleArbitrator(name, param)
    elseif args.arbitrator == "pipelined" then
      local arbitrator_param = {
        num_read_collect_units = NUM_RCUS,
        num_write_collect_units = NUM_WCUS,
        num_regfile_banks = NUM_REGFILE_BANKS,
        num_subcores = NUM_SUBCORES,
        num_shared_ports = NUM_SHARED_PORTS,
        num_read_ports = NUM_READ_PORTS,
        num_write_ports = NUM_WRITE_PORTS,
        rf_arch = ARBITRATOR_CONFIG,
      }
      return simtix.PipelinedArbitrator(name, param, arbitrator_param)
    end
  end)
end
tester.clock = clock
tester:test(args.seed, args.total_issued)
lv.info("Pass!")
