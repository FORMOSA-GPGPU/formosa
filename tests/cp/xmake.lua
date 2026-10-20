-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- Register the platform module as a configuration input, not just a hook import.
includes("platform.lua")

target("cp.lua", function()
  set_kind("phony")
  add_rules("lv.lua", { namespaces = { cp = "." } })
end)

target("lv", function() add_deps("cp.lua") end)

includes(
  "runtime/xmake.lua",
  "riscv-test/xmake.lua",
  "interrupt/xmake.lua",
  "prog0/xmake.lua",
  "benchmark/xmake.lua",
  "coremark/xmake.lua",
  "dhrystone/xmake.lua"
)
