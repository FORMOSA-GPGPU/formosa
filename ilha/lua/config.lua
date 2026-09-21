-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local addr = require("ilha.addr_map")

local function copy(value)
  if type(value) ~= "table" then return value end
  local result = {}
  for key, item in pairs(value) do
    result[key] = copy(item)
  end
  return result
end

local function is_power_of_two(value)
  if value <= 0 or value % 1 ~= 0 then return false end
  while value > 1 and value % 2 == 0 do
    value = value / 2
  end
  return value == 1
end

local function is_array(value)
  if #value == 0 then return false end
  for key in pairs(value) do
    if type(key) ~= "number" then return false end
  end
  return true
end

local function merge(target, overrides, path, allow_new)
  for key, value in pairs(overrides) do
    local current = rawget(target, key)
    assert(allow_new or current ~= nil, "unknown Ilha configuration field: " .. path .. key)
    if
      type(current) == "table"
      and type(value) == "table"
      and not is_array(current)
      and not is_array(value)
    then
      merge(current, value, path .. key .. ".", allow_new)
    else
      target[key] = copy(value)
    end
  end
end

---@class ilha.system_config
local SystemConfig = {
  dram_config = require("lv.util").runfile("configs/dram/apccas2026-lpddr4.json"),
  clock_period_ns = 2.5,
  num_sm = 1,
  threads_per_warp = 4,
  warps_per_core = 16,
  wg_resident_limit = 8,
  stack_remap_entries = 8,
  -- 0 selects threads_per_core; positive values explicitly override it.
  stack_remap_group_size = 0,
  shared_cache_size = 0x20000,
  cache_block_size = 64,
  non_cacheable_regions = {},
}
SystemConfig.__index = SystemConfig

function SystemConfig:threads_per_core() return self.threads_per_warp * self.warps_per_core end

function SystemConfig:effective_stack_remap_group_size()
  return self.stack_remap_group_size ~= 0 and self.stack_remap_group_size or self:threads_per_core()
end

---@return {addr: integer, size: integer}[]
function SystemConfig:effective_non_cacheable_regions()
  if #self.non_cacheable_regions > 0 then return self.non_cacheable_regions end
  return {
    { addr = addr.sm_printbuf_base, size = self:threads_per_core() },
    { addr = addr.noncache_region_base, size = addr.noncache_region_size },
  }
end

function SystemConfig:validate()
  assert(
    self.num_sm >= 1
      and self.num_sm <= addr.sm_mmio_aperture / addr.sm_mmio_stride
      and self.num_sm % 1 == 0,
    "system.num_sm must fit the fixed SM MMIO aperture"
  )
  assert(self.threads_per_warp > 0 and self.warps_per_core > 0)
  local group_size = self:effective_stack_remap_group_size()
  assert(is_power_of_two(group_size), "system.stack_remap_group_size must be a power of two")
  assert(
    self:threads_per_core() % group_size == 0,
    "system.stack_remap_group_size must divide threads_per_core"
  )
  assert(
    self.stack_remap_entries >= self.wg_resident_limit,
    "system.stack_remap_entries must be >= wg_resident_limit"
  )
  assert(
    self.stack_remap_entries <= addr.stack_remap_max_entries,
    "system.stack_remap_entries must not exceed the MMIO ABI maximum"
  )
end

--- APCCAS2026 is the complete default configuration. Runners apply file and
--- command-line overrides to this singleton before constructing one system.
---@class ilha.config
local Config = {
  ---@type ilha.system_config
  system = SystemConfig,
  sm = {
    module = "simtix.pipelined_sm",
    ---@type simtix.pipelined_sm.param
    param = {
      dcache = {
        cache_size_bytes = 0x4000,
        write_hit_policy = "WriteBack",
        mshr_entries = 8,
      },
    },
  },
}

---@param overrides {system?: ilha.system_config, sm?: {module?: string, param?: table<string, any>}}
---@return ilha.config
function Config:override(overrides)
  for key in pairs(overrides) do
    assert(key == "system" or key == "sm", "unknown Ilha configuration field: " .. key)
  end
  if overrides.system then merge(self.system, overrides.system, "system.", false) end
  if overrides.sm then
    for key in pairs(overrides.sm) do
      assert(key == "module" or key == "param", "unknown Ilha configuration field: sm." .. key)
    end
    if overrides.sm.module and overrides.sm.module ~= self.sm.module then self.sm.param = {} end
    if overrides.sm.module then self.sm.module = overrides.sm.module end
    if overrides.sm.param then merge(self.sm.param, overrides.sm.param, "sm.param.", true) end
  end
  return self
end

function Config:validate()
  self.system:validate()
  assert(type(self.sm.module) == "string" and self.sm.module ~= "", "sm.module is required")
  assert(type(self.sm.param) == "table", "sm.param must be a table")
end

return Config
