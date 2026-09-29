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
local function wait_word(addr, expected)
  for _ = 1, 128 do
    system:step(64)
    if word(addr) == expected then return end
  end
  error(string.format("interrupt test: expected %d at 0x%x, got %d", expected, addr, word(addr)))
end
local ok, err = pcall(function()
  local elf = workload.ELF(elf_path)
  assert(system:load_elf(elf))
  system:boot(elf.entry)
  wait_word(0x3004, 1)
  for count = 1, 2 do
    ext_int:write(true)
    wait_word(0x3000, count)
    ext_int:write(false)
    wait_word(0x3008, count)
    assert(word(0x3000) == count, "interrupt counted more than once")
  end
end)
system:close()
assert(ok, err)
print(args.core .. ": two external interrupts handled and returned")
