-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

local parser = require("argparse")("runtime/test.lua", "Check the standalone CP C runtime.")
parser:option("--core", "CPU adapter module"):count(1)
local args = parser:parse({ ... })
local System = require("cp.system")
local Core = require(args.core)
local arch = Core.isa:match("^(rv32im)") or Core.isa:match("^(rv64im)")
assert(arch, "runtime test requires RV32IM or RV64IM")
local value = arch == "rv64im" and "4294967296" or "4294967295"
local expected = "runtime: -7 " .. value .. " 002a X %\nruntime: PASS\n"
local elf = require("lv.util").runfile("tests/cp/runtime/" .. arch .. ".elf")
local system = System("test", args.core)
system:run(elf, function(cpu)
  local output = cpu:output()
  assert(not output:find("runtime: FAIL", 1, true), "CP runtime reported failure")
  if output:find("runtime: PASS\n", 1, true) then
    assert(output == expected, "incorrect CP runtime output")
    return true
  end
end, { chunk_cycles = 64, max_cycles = 100000 })
