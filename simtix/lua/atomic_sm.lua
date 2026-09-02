-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

---@class simtix.atomic_sm : formosa.system.sm
---@field protected _clock sc.clock
---@field protected _reset_n sc.signal
---@field protected _id integer
---@field protected _sc_module sc.Module
---@field protected _router simple.XBar
---@field protected _core simtix.AtomicCore
---@field protected _dmem_xbar simple.XBar
---@field protected _local_mem simtix.AtomicMemory
---@field protected _l1cache simtix.Cache
---@field protected _stub_cache formosa.StubCacheMmio
---@field protected _wg_init formosa.WGInitializer
---@field protected _core_info simple.ConstantTable
---@field protected _stack_remap simtix.StackRemapTable
---@overload fun(name: string, config: formosa.system.config, clock: sc.clock, reset_n: sc.signal, id: integer): simtix.atomic_sm
local AtomicSM = {}

---@param name string
---@param config formosa.system.config
---@param clock sc.clock
---@param reset_n sc.signal
---@param id integer
---@return simtix.atomic_sm
function AtomicSM.new(name, config, clock, reset_n, id)
  config = config or {}
  ---@type simtix.atomic_sm
  local self = setmetatable({}, AtomicSM --[[@as table]])
  self._clock = clock
  self._reset_n = reset_n
  self._id = id

  -- Child names are local to this hierarchy-aware SM instance.
  self._core = simtix.AtomicCore("AtomicCore", {
    num_warps = config.warps_per_core,
    num_lanes = config.threads_per_warp,
  })

  self._l1cache = simtix.Cache("L1Cache", {
    write_hit_policy = "WriteBack",
    size_bytes = config.cache_size,
    block_size_bytes = config.cache_block_size,
    non_cacheable_regions = config.non_cacheable_regions or {},
  })
  local lmem_window = config.local_mem_window or config.local_mem_size
  self._dmem_xbar = simple.XBar("DMemXBar", 1, {
    { addr = 0x0, size = config.local_mem_size }, -- usable local memory
    {
      addr = lmem_window,
      size = (config.max_size - lmem_window + 1),
      subtract_start_addr = false,
    }, -- system map identity
  })
  self._local_mem = simtix.AtomicMemory("LocalMem", {
    size = config.local_mem_size,
  })

  self._stub_cache = formosa.StubCacheMmio("StubCache", {
    verbose = false,
  })

  self._wg_init = formosa.WGInitializer("wg_init", {
    warps_per_core = config.warps_per_core,
    threads_per_warp = config.threads_per_warp,
    wg_resident_limit = config.wg_resident_limit,
    fifo_size = 4,
    enable_trace = true,
  })

  self._core_info = simple.ConstantTable("CoreInfo", {
    entries = {
      { addr = 0x00, size = 8, value = config.threads_per_core }, -- Max threads per core
      { addr = 0x08, size = 8, value = config.stack_remap_entries }, -- Stack remap entries
      { addr = 0x10, size = 8, value = config.stack_remap_group_size }, -- Stack remap group size
    },
  })

  -- MMIO compatibility; AtomicCore ignores stack-remap descriptors.
  self._stack_remap = simtix.StackRemapTable("StackRemapTable", {
    entries = config.stack_remap_entries,
    region_size = config.stack_size_per_thread * config.threads_per_core,
  })

  self._router = simple.XBar("SMRouter", 1, {
    { addr = config.wgi_csr_base, size = config.wgi_csr_size }, -- WGInit
    { addr = config.icache_csr_base, size = config.cache_csr_size }, -- StubCache (I-Cache)
    { addr = config.dcache_csr_base, size = config.cache_csr_size }, -- L1Cache (D-Cache)
    { addr = config.core_csr_base, size = config.core_csr_size }, -- Core Info Read-only
    {
      addr = config.stack_remap_csr_base,
      size = config.stack_remap_csr_size,
    }, -- Stack remap descriptors
  })

  self._router.mem_side[1].target = self._wg_init.port
  self._router.mem_side[2].target = self._stub_cache.mmio_port
  self._router.mem_side[3].target = self._l1cache.mmio_port
  self._router.mem_side[4].target = self._core_info.port
  self._router.mem_side[5].target = self._stack_remap.mmio_port

  self._core.target = self._dmem_xbar.core_side[1].port
  self._dmem_xbar.mem_side[1].target = self._local_mem.port
  self._dmem_xbar.mem_side[2].target = self._l1cache.port
  self._core:sched_init(
    function(name)
      return simtix.Lrr({
        num_warps = config.warps_per_core,
      })
    end
  )
  self._wg_init.warp_ctrl_target = self._core.warp_ctrl

  self.stats = stats.Group("AtomicSM" .. id)
  self.stats:add_sub_group(self._core.stats)
  self.stats:add_sub_group(self._l1cache.stats)

  self._core.clock = self._clock
  self._router.clock = self._clock
  self._stub_cache.clock = self._clock
  self._l1cache.clock = self._clock
  self._dmem_xbar.clock = self._clock
  self._local_mem.clock = self._clock

  return self
end

function AtomicSM:set_target(target) self._l1cache.target = target end

function AtomicSM:get_port() return self._router.core_side[1].port end

function AtomicSM:__index(key)
  if key == "port" then
    return self:get_port()
  else
    return AtomicSM[key]
  end
end

function AtomicSM:__newindex(key, value)
  if key == "target" then
    self:set_target(value)
  else
    rawset(self, key, value)
  end
end

setmetatable(AtomicSM --[[@as table]], {
  __call = function(cls, ...) --Call constructor
    return cls.new(...)
  end,
})

return require("lv.sc_module").wrap(AtomicSM)
