-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local util = require("lv.util")
assert(type(require("lv.sc_module")) == "table")
local configs = util.runfile("third-party/DRAMSys/configs")
assert(dramsys.RESOURCE_DIR == configs)
local config = assert(io.open(configs .. "/ddr4-example.json", "r"))
assert(config:read("*a"):find("memspec", 1, true))
config:close()
print("LV resources OK")
