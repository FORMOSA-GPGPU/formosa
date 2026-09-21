-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

__RUNFILE_PATH__ = assert(arg[1]) .. "/"
local source_dir = assert(arg[2])

---@type ilha.config
local Config = require("ilha.config")
local stdlib = require("posix.stdlib")

Config:override({
  system = { threads_per_warp = 8 },
  sm = {
    param = {
      core = { heartbeat_frequency = 42 },
      dcache = { cache_size_bytes = 0x8000 },
    },
  },
})
assert(Config.system:threads_per_core() == 128)
assert(Config.sm.param.core.heartbeat_frequency == 42)

Config:override({
  sm = {
    param = {
      dcache = { non_cacheable_regions = { { addr = 1, size = 2 } } },
    },
  },
})
Config:override({ sm = { param = { dcache = { ways = 8, non_cacheable_regions = {} } } } })
assert(Config.sm.param.dcache.cache_size_bytes == 0x8000, "named tables must merge recursively")
assert(Config.sm.param.dcache.ways == 8)
assert(#Config.sm.param.dcache.non_cacheable_regions == 0, "arrays must be replaced as a whole")

local ok, message = pcall(function() Config:override({ system = { threads_per_wrap = 8 } }) end)
assert(not ok)
assert(tostring(message):match("system%.threads_per_wrap"))

local overridden = require("ilha.config_cli").load({
  config = "large_sm",
  system_config = source_dir .. "/tests/data/system_override.lua",
  sm = "simtix.atomic_sm",
  sm_param = source_dir .. "/tests/data/atomic_sm_param.lua",
})
assert(overridden.system.num_sm == 2)
assert(overridden.system.threads_per_warp == 32)
assert(overridden.system.warps_per_core == 32)
assert(overridden.system:threads_per_core() == 1024)
assert(overridden.sm.module == "simtix.atomic_sm")
assert(overridden.sm.param.non_cacheable_regions[1].addr == 0x23000)
assert(
  overridden.system.dram_config
    == stdlib.realpath(source_dir .. "/configs/dram/large-sm-lpddr4.json"),
  "system override resources must resolve relative to the override file"
)

print("Ilha config tests passed")
