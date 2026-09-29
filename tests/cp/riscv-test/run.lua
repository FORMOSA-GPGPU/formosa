-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

local parser = require("argparse")("riscv-test/run.lua", "Run the RISC-V ISA tests on a CPU model.")
parser:option("--core", "Core module, e.g. cp.command_processor or simtix.scalar_core"):count(1)
parser:option("--elf", "Use a custom test ELF with a matching .tests file beside it")
parser
  :option("--max-cycles", "Maximum simulated cycles before failing")
  :convert(function(value)
    local n = tonumber(value)
    if n and n >= 1 and n < math.huge and n % 1 == 0 then return n end
    return nil, "expected a positive integer"
  end)
  :default("2097152")
local args = parser:parse({ ... })
local System = require("cp.system")
local ok, Core = pcall(require, args.core)
if not ok then parser:error(Core) end
local elf_path = args.elf
  or require("lv.util").runfile("tests/cp/riscv-test/" .. Core.isa .. ".elf")
local manifest_path = elf_path:gsub("%.elf$", "") .. ".tests"
for _, path in ipairs({ elf_path, manifest_path }) do
  local file = io.open(path, "rb")
  if not file then
    return parser:error(
      "Missing test workload: "
        .. path
        .. "\nBuild the CPU tests for this core, or use --elf to select a test ELF."
    )
  end
  file:close()
end
local ok_check, check = pcall(require("cp.riscv-test.check").checker, manifest_path)
if not ok_check then parser:error(check) end
local system = System("test", args.core)
local count = system:run(elf_path, check, { max_cycles = args.max_cycles })
print(string.format("%s: all %d RISC-V tests passed", args.core, count))
