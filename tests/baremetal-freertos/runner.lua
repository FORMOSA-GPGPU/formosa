-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

-- Shared runner; program directories supply their checker and core requirements.
return function(argv, program, check, requirements)
  local parser = require("argparse")(program .. "/run.lua", "Run " .. program .. " on a CPU model.")
  parser:option("--core", "CPU adapter module"):count(1)
  parser:option("--elf", "Use a custom program ELF")
  parser
    :option("--max-cycles", "Maximum simulated cycles before failing")
    :convert(function(value)
      local n = tonumber(value)
      if n and n >= 1 and n < math.huge and n % 1 == 0 then return n end
      return nil, "expected a positive integer"
    end)
    :default("2097152")
  local args = parser:parse(argv)
  local System = require("cp.system")
  local ok, Core = pcall(require, args.core)
  if not ok then return parser:error(Core) end
  if not Core.isa:match("^rv64im") then return parser:error(program .. " requires RV64IM") end
  for _, capability in ipairs(requirements or {}) do
    if not Core[capability] then return parser:error(program .. " requires " .. capability) end
  end
  local elf = args.elf
    or require("lv.util").runfile(
      "tests/baremetal-freertos/" .. program .. "/" .. program .. ".elf"
    )
  local system = System("test", args.core, require("baremetal-freertos.platform"))
  system:run(elf, check, { max_cycles = args.max_cycles, chunk_cycles = 4096 })
  print(args.core .. ": " .. program .. " passed")
end
