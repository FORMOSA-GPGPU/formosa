-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

local parser = require("argparse")(
  "interrupt/run.lua",
  "Check two external interrupts and return to the main loop on a CPU model."
)
parser:option("--core", "Core module with has_ext_int, e.g. cp.command_processor"):count(1)
parser:option("--elf", "Use a custom interrupt-test ELF")
local args = parser:parse({ ... })
local System = require("cp.system")
local ok_core, Core = pcall(require, args.core)
if not ok_core then return parser:error(Core) end
if not Core.has_ext_int then
  return parser:error(args.core .. " does not support external interrupts (has_ext_int=false)")
end
local xlen = Core.isa:match("^rv(32)") or Core.isa:match("^rv(64)")
if not xlen then return parser:error("Unsupported interrupt-test ISA: " .. Core.isa) end
local elf_path = args.elf or require("lv.util").runfile("tests/cp/interrupt/rv" .. xlen .. ".elf")
local file = io.open(elf_path, "rb")
if not file then
  return parser:error(
    "Missing interrupt test ELF: " .. elf_path .. "\nBuild the CPU tests for this core first."
  )
end
file:close()
local system = System("test", args.core)
local ext_int = assert(system.ext_int, "core did not provide its advertised ext_int input")
local function word(addr)
  local bytes = system:read_bytes(addr, 4)
  return bytes[1] + bytes[2] * 256 + bytes[3] * 65536 + bytes[4] * 16777216
end
local phase, count = "ready", 1
system:run(elf_path, function()
  if phase == "ready" then
    if word(0x3004) ~= 1 then return nil end
    ext_int:write(true)
    phase = "handler"
  elseif phase == "handler" then
    if word(0x3000) ~= count then return nil end
    ext_int:write(false)
    phase = "return"
  else
    if word(0x3008) ~= count then return nil end
    assert(word(0x3000) == count, "interrupt counted more than once")
    if count == 2 then return true end
    count = count + 1
    ext_int:write(true)
    phase = "handler"
  end
end, { chunk_cycles = 64, max_cycles = 5 * 8192 })
print(args.core .. ": two external interrupts handled and returned")
