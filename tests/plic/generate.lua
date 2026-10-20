-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- CMake's Lua CLI uses the same templates as native Xmake configfiles.
local directory = assert(arg[1], "expected output directory")
local here = arg[0]:match("^(.*)/") or "."
local platform = require("plic.platform")
local variables = {}
for key, value in pairs(platform) do
  variables["PLIC_TEST_" .. key:upper()] = string.format("0x%x", value)
end
variables.PLIC_TEST_RAM_END = string.format("0x%x", platform.ram_base + platform.ram_size)
for _, name in ipairs({ "platform.h", "linker.ld" }) do
  local template = assert(io.open(here .. "/" .. name .. ".in", "r"))
  local contents = template:read("*a")
  template:close()
  contents = contents:gsub(
    "@([%w_]+)@",
    function(key) return assert(variables[key], "missing PLIC platform field: " .. key) end
  )
  local file = assert(io.open(directory .. "/" .. name, "w"))
  assert(file:write(contents))
  assert(file:close())
end
