-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

target("simtix", function()
  set_kind("object")
  -- Keep simulation fast; Debug/Coverage still retain symbols and assertions.
  set_optimize("fastest")
  add_files("src/**.cc")
  add_includedirs("src", { public = true })
  add_deps("libilha", "liblv", "softfloat", { public = true })
end)

target("simtix.lua", function()
  set_kind("phony")
  add_rules("lv.lua", { namespaces = { simtix = "lua" } })
end)

target("lv", function() add_deps("simtix", "simtix.lua") end)

includes("tests/xmake.lua")
