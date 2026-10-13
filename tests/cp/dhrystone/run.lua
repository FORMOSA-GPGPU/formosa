-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

local parser = require("argparse")("dhrystone/run.lua", "Validate and measure Dhrystone.")
parser:option("--core", "CPU adapter module"):count(1)
parser:option("--elf", "Use a custom Dhrystone ELF with the same mailbox protocol")
parser
  :option("--max-cycles", "Maximum simulated cycles before failing")
  :convert(function(value)
    local n = tonumber(value)
    if n and n >= 1 and n < math.huge and n % 1 == 0 then return n end
    return nil, "expected a positive integer"
  end)
  :default("50000000")
local args = parser:parse({ ... })
local System = require("cp.system")
local ok, Core = pcall(require, args.core)
if not ok then return parser:error(Core) end
local arch = Core.isa:match("^(rv32im)") or Core.isa:match("^(rv64im)")
if not arch then return parser:error("Dhrystone requires RV32IM or RV64IM") end
local elf = args.elf or require("lv.util").runfile("tests/cp/dhrystone/" .. arch .. ".elf")
local system = System("test", args.core)
local result = system:run(elf, require("cp.benchmark.check").checker(system), {
  max_cycles = args.max_cycles,
  chunk_cycles = 1,
})
print(
  string.format(
    "%s: dhrystone PASS iterations=%d cycles=%d score=%.9f DMIPS/MHz (short simulation run)",
    args.core,
    result.iterations,
    result.cycles,
    1e6 * result.iterations / (result.cycles * 1757)
  )
)
