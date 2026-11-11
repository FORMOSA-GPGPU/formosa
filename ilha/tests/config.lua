-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local source_dir = assert(...)

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

-- Global allocations start at the first aligned address past the stacks of
-- every resident work-group slot: 3 SMs * 8 slots * 1024 threads.
local addr = require("ilha.addr_map")
overridden:override({ system = { num_sm = 3 } })
local stacks_end = addr.stack_base + 3 * 8 * 1024 * addr.per_thread_stack_size
local alloc_base = overridden.system:global_alloc_base()
assert(alloc_base % addr.global_alloc_align == 0)
assert(alloc_base >= stacks_end and alloc_base < stacks_end + addr.global_alloc_align)
overridden:validate()

-- 8 SMs * 8 slots * 32768 threads of stacks would cover global memory.
overridden:override({ system = { num_sm = 8, warps_per_core = 1024 } })
ok, message = pcall(function() overridden:validate() end)
assert(not ok)
assert(tostring(message):match("must leave global memory"))

-- The shared cache uses the generic cache parameter schema.
local default_cache = Config.system:shared_cache_param()
assert(default_cache.ways == 8)
assert(default_cache.set_index_function == "ipoly")
assert(default_cache.mshr_entries == 16)
assert(default_cache.write_hit_policy == "WriteBack")
assert(default_cache.cache_size_bytes == Config.system.shared_cache.cache_size_bytes)
assert(default_cache.block_size_bytes == Config.system.cache_block_size)
assert(default_cache.atomic_linearization == true)
local regions = Config.system:effective_non_cacheable_regions()
for i, region in ipairs(regions) do
  assert(default_cache.non_cacheable_regions[i].addr == region.addr)
  assert(default_cache.non_cacheable_regions[i].size == region.size)
end
Config:override({ system = { shared_cache = { ways = 4, set_index_function = "modulo" } } })
local configured_cache = Config.system:shared_cache_param()
assert(configured_cache.ways == 4)
assert(configured_cache.set_index_function == "modulo")
for key, value in pairs(default_cache) do
  if key == "non_cacheable_regions" then
    for i, region in ipairs(value) do
      assert(configured_cache[key][i].addr == region.addr)
      assert(configured_cache[key][i].size == region.size)
    end
  elseif key ~= "ways" and key ~= "set_index_function" then
    assert(configured_cache[key] == value)
  end
end
local cache_ok, cache_message = pcall(
  function() Config:override({ system = { shared_cache = { wayz = 4 } } }) end
)
assert(not cache_ok)
assert(tostring(cache_message):match("system%.shared_cache%.wayz"))

-- The L2 victim buffer follows its MSHR count unless configured explicitly.
assert(default_cache.victim_buffer_entries == default_cache.mshr_entries)
Config:override({ system = { shared_cache = { victim_buffer_entries = 4 } } })
assert(Config.system:shared_cache_param().victim_buffer_entries == 4)

print("Ilha config tests passed")
