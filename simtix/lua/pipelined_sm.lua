-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local BankedMemory = require("simtix.banked_memory")

---@class simtix.pipelined_sm.lsu_config
---@field num_inflight_slots? integer Maximum number of in-flight LSU instructions.

---@class simtix.pipelined_sm.param
---@field core? simtix.PipelinedCore.Param
---@field icache? simtix.Cache.Param
---@field dcache? simtix.Cache.Param
---@field num_lmem_banks? integer
---@field scheduler? "lrr"|"gto"|"tl"
---@field scheduler_config? simtix.TwoLevel.Param
---@field lsu? "simple"|"coalescing"|"coalescing_outstanding"
---@field lsu_config? simtix.pipelined_sm.lsu_config

---@class simtix.pipelined_sm : ilha.system.sm
---@field protected _clock sc.clock
---@field protected _id integer
---@field protected _sc_module sc.Module
---@field protected _router simple.XBar
---@field protected _core simtix.PipelinedCore
---@field protected _icache simtix.Cache
---@field protected _dcache simtix.Cache
---@field protected _dmem_mux simple.Mux
---@field protected _dmem_xbars simple.XBar[]
---@field protected _local_mem simtix.BankedMemory
---@field protected _mux simple.Mux
---@field protected _wg_init ilha.WGInitializer
---@field protected _core_info simple.ConstantTable
---@field protected _stack_remap simtix.StackRemapTable
---@overload fun(name: string, id: integer, config: ilha.system_config, sm_param?: simtix.pipelined_sm.param): simtix.pipelined_sm
local PipelinedSM = {}

---@param name string
---@param id integer
---@param config ilha.system_config
---@param sm_param? simtix.pipelined_sm.param
---@return simtix.pipelined_sm
function PipelinedSM.new(name, id, config, sm_param)
  sm_param = sm_param or {}
  ---@type simtix.pipelined_sm
  local self = setmetatable({}, PipelinedSM --[[@as table]])
  self._id = id
  local addr = require("ilha.addr_map")
  local threads_per_core = config:threads_per_core()

  ---@type simtix.Cache.Param
  local icache_param = sm_param.icache or {}
  icache_param.block_size_bytes = icache_param.block_size_bytes or config.cache_block_size

  ---@type simtix.Cache.Param
  local dcache_param = sm_param.dcache or {}
  dcache_param.block_size_bytes = dcache_param.block_size_bytes or config.cache_block_size
  dcache_param.non_cacheable_regions = config:effective_non_cacheable_regions()

  local core_param = {
    num_warps = config.warps_per_core,
    num_lanes = config.threads_per_warp,
  }

  local pipe_param = sm_param.core or {}

  -- Child names are local to this hierarchy-aware SM instance.
  self._core = simtix.PipelinedCore("PipelinedCore", core_param, pipe_param)
  local num_subcores = #self._core.subcores

  self._icache = simtix.Cache("ICache", icache_param)
  self._dcache = simtix.Cache("DCache", dcache_param)

  self._dmem_mux = simple.Mux("DCacheMux", { fifo_size = 32 })
  -- SM-private data map: LMEM, then identity map for on-chip GMEM / DDR.
  -- Cutover is local_mem_window (64 KiB), not sizeof usable LMEM (48 KiB).
  local lmem_window = addr.lmem_window
  self._dmem_xbars = {}
  for i = 1, num_subcores do
    self._dmem_xbars[i] = simple.XBar("DMemXBar" .. (i - 1), 1, {
      { addr = 0x0, size = addr.lmem_size }, -- usable local memory
      {
        addr = lmem_window,
        size = (addr.max_size - lmem_window + 1),
        subtract_start_addr = false,
      }, -- system map (GMEM @ 0x100000, DDR @ 0x80000000, ...)
    })
  end
  self._local_mem = BankedMemory("LocalMem", {
    size = addr.lmem_size,
    num_banks = sm_param.num_lmem_banks or num_subcores,
    num_froms = num_subcores,
    bank_line_size = dcache_param.block_size_bytes,
  })

  self._mux = simple.Mux("Mux", { fifo_size = 32 })

  self._wg_init = ilha.WGInitializer("wg_init", {
    warps_per_core = config.warps_per_core,
    threads_per_warp = config.threads_per_warp,
    wg_resident_limit = config.wg_resident_limit,
    fifo_size = 4,
    enable_trace = true,
  })

  self._core_info = simple.ConstantTable("CoreInfo", {
    entries = {
      { addr = 0x00, size = 8, value = threads_per_core }, -- Max threads per core
      { addr = 0x08, size = 8, value = config.stack_remap_entries }, -- Stack remap entries
      { addr = 0x10, size = 8, value = config:effective_stack_remap_group_size() },
    },
  })

  local stack_remap = simtix.StackRemapTable("StackRemapTable", {
    entries = config.stack_remap_entries,
    region_size = addr.per_thread_stack_size * threads_per_core,
  })
  self._stack_remap = stack_remap

  self._router = simple.XBar("SMRouter", 1, {
    { addr = addr.wgi_csr_base, size = addr.wgi_csr_size }, -- WGInit
    { addr = addr.icache_csr_base, size = addr.cache_csr_size }, -- I-Cache
    { addr = addr.dcache_csr_base, size = addr.cache_csr_size }, -- D-Cache
    { addr = addr.core_csr_base, size = addr.core_csr_size }, -- Core Info Read-only
    {
      addr = addr.stack_remap_csr_base,
      size = addr.stack_remap_csr_size,
    }, -- Stack remap descriptors
  })

  -- Connections
  self._router.mem_side[1].target = self._wg_init.port
  self._router.mem_side[2].target = self._icache.mmio_port
  self._router.mem_side[3].target = self._dcache.mmio_port
  self._router.mem_side[4].target = self._core_info.port
  self._router.mem_side[5].target = self._stack_remap.mmio_port

  self._core.imem = self._icache.port
  self._dmem_mux.to = self._dcache.port

  -- Subcore (backends) configuration
  local scheduler = sm_param.scheduler or "tl"
  local scheduler_config = sm_param.scheduler_config or {}

  assert(
    scheduler == "tl" or next(scheduler_config) == nil,
    "sm.param.scheduler_config requires the tl scheduler"
  )
  local lsu_kind = sm_param.lsu or "coalescing_outstanding"
  local lsu_config = sm_param.lsu_config or {}

  assert(
    lsu_kind == "coalescing_outstanding" or next(lsu_config) == nil,
    "sm.param.lsu_config requires the coalescing_outstanding LSU"
  )
  for i = 1, #self._core.subcores do
    local subcore = self._core.subcores[i]
    subcore:sched_init(function(name)
      if scheduler == "lrr" then
        return simtix.Lrr(core_param)
      elseif scheduler == "gto" then
        return simtix.Gto(core_param)
      elseif scheduler == "tl" then
        return simtix.TwoLevel(core_param, scheduler_config)
      else
        error("Unknown scheduler: " .. tostring(scheduler))
      end
    end)

    subcore:lsu_init(function(name)
      if lsu_kind == "simple" then return simtix.SimpleLsu(name, core_param) end

      local lsu_param = {
        cache_block_size = dcache_param.block_size_bytes,
        enable_stack_remap = true,
        granularity = 8,
        stack_group_size = config:effective_stack_remap_group_size(),
        stack_start = addr.stack_base,
        stack_end = addr.global_alloc_base - 1,
        stack_size_per_thread = addr.per_thread_stack_size,
      }
      local lsu
      if lsu_kind == "coalescing" then
        lsu = simtix.CoalescingLsu(name, core_param, lsu_param)
      elseif lsu_kind == "coalescing_outstanding" then
        lsu_param.num_inflight_slots = lsu_config.num_inflight_slots
        lsu = simtix.CoalescingOutstandingLsu(name, core_param, lsu_param)
      else
        error("Unknown LSU: " .. tostring(lsu_kind))
      end
      lsu.stack_remap_table = stack_remap
      return lsu
    end)

    subcore:arbitrator_init(
      function(name)
        return simtix.PipelinedArbitrator(name, core_param, {
          num_read_collect_units = 4,
          num_write_collect_units = 1,
          rf_arch = "Baseline",
          num_regfile_banks = 4,
          num_subcores = num_subcores,
          num_shared_ports = 1,
          num_read_ports = 0,
          num_write_ports = 0,
          pftrace = false,
        })
      end
    )
    local dmem_xbar = self._dmem_xbars[i]
    self._core.subcores[i].dmem = dmem_xbar.core_side[1].port
    dmem_xbar.mem_side[1].target = self._local_mem.port
    dmem_xbar.mem_side[2].target = self._dmem_mux.from
  end

  self._wg_init.warp_ctrl_target = self._core.warp_ctrl

  -- Stats
  self.stats = stats.Group("PipelinedSM" .. id)
  self.stats:add_sub_group(self._core.stats)
  self.stats:add_sub_group(self._icache.stats)
  self.stats:add_sub_group(self._dcache.stats)

  self._icache.target = self._mux.from
  self._dcache.target = self._mux.from

  return self
end

function PipelinedSM:set_clock(clock)
  self._clock = clock
  self._core.clock = clock
  self._icache.clock = clock
  self._dcache.clock = clock
  for _, dmem_xbar in ipairs(self._dmem_xbars) do
    dmem_xbar.clock = clock
  end
  self._local_mem.clock = clock
  self._router.clock = clock
end

function PipelinedSM:set_target(target) self._mux.to = target end

function PipelinedSM:get_port() return self._router.core_side[1].port end

function PipelinedSM:__index(key)
  if key == "port" then
    return self:get_port()
  elseif key == "clock" then
    return self._clock
  else
    return PipelinedSM[key]
  end
end

function PipelinedSM:__newindex(key, value)
  if key == "target" then
    self:set_target(value)
  elseif key == "clock" then
    self:set_clock(value)
  else
    rawset(self, key, value)
  end
end

setmetatable(PipelinedSM --[[@as table]], {
  __call = function(cls, ...) -- Call constructor
    return cls.new(...)
  end,
})

return require("lv.sc_module").wrap(PipelinedSM)
