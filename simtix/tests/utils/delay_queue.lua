-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local argparse = require("argparse")

-- Parameters
local parser = argparse("delay_queue.lua")
parser:option("--latency", "DelayQueue latency"):default(8):convert(tonumber)
parser:option("--ticks-per-output", "DelayQueue throughput inverse"):default(8):convert(tonumber)
parser:option("--total-issued", "Total number of items to issue"):default(1000):convert(tonumber)
parser:option("--seed", "Random seed"):default(48763):convert(tonumber)

local args = parser:parse({ ... })

local LATENCY = args.latency
local TICKS_PER_OUTPUT = args.ticks_per_output

local clock = sc.clock("clock", 10, sc.time_unit.NS)
local tester = simtix.DelayQueueTester("tester", LATENCY, TICKS_PER_OUTPUT)
tester.clock = clock
tester:test(args.seed, args.total_issued)
print("Pass!")
