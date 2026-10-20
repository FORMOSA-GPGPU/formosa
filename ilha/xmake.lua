-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

includes("platform-data/xmake.lua")

target("libilha", function()
  set_kind("headeronly")
  add_includedirs("include", { public = true })
  add_deps("systemc", { public = true })
end)

target("ilha.bindings", function()
  set_kind("object")
  add_files("src/*.cc")
  add_deps("libilha", "liblv")
end)

target("ilha.lua", function()
  set_kind("phony")
  add_rules("lv.lua", { namespaces = { ilha = "lua" } })
end)

target("ilha.runfiles", function()
  set_kind("phony")
  local files = {}
  for _, source in ipairs(os.files("configs/**")) do
    files[source] = source
  end
  for _, source in ipairs({
    "addressmapping/am_lpddr4_1Gbx16_robaco.json",
    "mcconfig/fr_fcfs.json",
    "memspec/JEDEC_1Gbx16_LPDDR4-1600.json",
    "memspec/JEDEC_1Gbx16_LPDDR4-3200.json",
  }) do
    files["configs/dram/" .. path.filename(source)] = "../third-party/DRAMSys/configs/" .. source
  end
  add_rules("lv.runfiles", { files = files, copy = true })
end)

target("lv", function() add_deps("ilha.bindings", "ilha.lua", "ilha.runfiles") end)
target("ilha-daemon", function()
  add_rules("lv.script", { script = "daemon.lua" })
  add_deps("fw.runfiles", { inherit = false })
end)

includes("tests/xmake.lua")
