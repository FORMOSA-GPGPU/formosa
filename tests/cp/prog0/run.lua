-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

local parser = require("argparse")("prog0/run.lua", "Run prog0 and compare its memory results.")
parser:option("--core", "Core module, e.g. cp.command_processor or simtix.scalar_core"):count(1)
parser:option("--elf", "Use a custom prog0 ELF")
parser:option("--golden", "45-word golden file"):count(1)
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
if not ok then return parser:error(Core) end
local xlen = Core.isa:match("^rv(32)i") or Core.isa:match("^rv(64)i")
if not xlen then return parser:error("prog0 requires an RV32I or RV64I core") end
local elf_path = args.elf or require("lv.util").runfile("tests/cp/prog0/rv" .. xlen .. ".elf")
local golden_path = args.golden
local elf_file = io.open(elf_path, "rb")
if not elf_file then
  return parser:error("Missing prog0 ELF: " .. elf_path .. "\nBuild the CPU tests or use --elf.")
end
elf_file:close()
local file = io.open(golden_path, "r")
if not file then return parser:error("Missing prog0 golden: " .. golden_path) end
local contents = file:read("*a")
file:close()
local golden = {}
for line in contents:gmatch("[^\r\n]+") do
  line = line:match("^%s*(.-)%s*$")
  if line ~= "" then
    if #line ~= 8 or not line:match("^%x+$") then
      return parser:error("Expected an eight-digit hexadecimal word in " .. golden_path)
    end
    golden[#golden + 1] = assert(tonumber(line, 16))
  end
end
if #golden ~= 45 then return parser:error("prog0 golden must contain exactly 45 words") end
local system = System("test", args.core)
local function word(addr)
  local bytes = system:read_bytes(addr, 4)
  return bytes[1] + bytes[2] * 256 + bytes[3] * 65536 + bytes[4] * 16777216
end
local count = system:run(elf_path, function()
  if word(0x7FFC) ~= 0xFFFFFFFF then return nil end
  for i, expected in ipairs(golden) do
    local addr = 0x8000 + (i - 1) * 4
    local actual = word(addr)
    assert(
      actual == expected,
      string.format("prog0: address 0x%04x: expected %08x, got %08x", addr, expected, actual)
    )
  end
  return #golden
end, { max_cycles = args.max_cycles })
print(string.format("%s: prog0 passed, all %d words matched", args.core, count))
