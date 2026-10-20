-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

-- Execution platform shared by CPU test programs and their build scripts.
local platform = {
  reset_cycles = 5,
  ram_base = 0,
  ram_size = 0x200000,
  text_base = 0x10000,
  stack_base = 0x4000,
  serial_base = 0x200000,
}

-- Xmake imports exported functions; Lua require consumes the table below.
if xmake then
  function main() return platform end
end

return platform
