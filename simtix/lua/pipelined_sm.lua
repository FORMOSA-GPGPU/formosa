-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local BankedMemory = require("simtix.banked_memory")

---@class simtix.pipelined_sm.config : formosa.system.config
---@field pipelined_core_config simtix.PipelinedCore.Param
---@field icache_block_size integer
---@field dcache_block_size integer
---@field num_lmem_banks? integer
---@field scheduler? string
---@field scheduler_config? table
---@field enable_ghost_scheduler? boolean
---@field ghost_scheduler_param? simtix.PipelinedCore.GhostParam

---@class simtix.pipelined_sm : formosa.system.sm
---@field protected _clock sc.clock
---@field protected _reset_n sc.signal
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
---@field protected _wg_init formosa.WGInitializer
---@field protected _core_info simple.ConstantTable
---@field protected _stack_remap simtix.StackRemapTable
---@overload fun(name: string, config: simtix.pipelined_sm.config, clock: sc.clock, reset_n: sc.signal, id: integer): simtix.pipelined_sm
local PipelinedSM = {}

---@param name string
---@param config simtix.pipelined_sm.config
---@param clock sc.clock
---@param reset_n sc.signal
---@param id integer
---@return simtix.pipelined_sm
function PipelinedSM.new(name, config, clock, reset_n, id)
  config = config or {}
  ---@type simtix.pipelined_sm
  local self = setmetatable({}, PipelinedSM --[[@as table]])
  self._clock = clock
  self._reset_n = reset_n
  self._id = id

  local core_param = {
    num_warps = config.warps_per_core,
    num_lanes = config.threads_per_warp,
  }

  local pipe_param = config.pipelined_core_config
    or {
      fetch_width = 2,
      decode_width = 2,
      num_subcores = 4,
      num_fetch_filter_entries = 8,
      num_fetch_entries = 8,
      enable_iwis = true,
      pftrace = false,
    }

  if config.enable_ghost_scheduler ~= nil then
    pipe_param.enable_ghost_scheduler = config.enable_ghost_scheduler
  end

  local ghost_scheduler_param = config.ghost_scheduler_param
  pipe_param.ghost_param = ghost_scheduler_param or {}

  -- Child names are local to this hierarchy-aware SM instance.
  self._core = simtix.PipelinedCore("PipelinedCore", core_param, pipe_param)
  local num_subcores = #self._core.subcores

  self._icache = simtix.Cache("ICache", {
    write_hit_policy = "WriteThrough",
    size_bytes = config.icache_size or config.cache_size,
    block_size_bytes = config.icache_block_size or config.cache_block_size,
    ways = 4,
    mshrs = 4,
  })

  self._dcache = simtix.Cache("DCache", {
    write_hit_policy = "WriteBack",
    size_bytes = config.dcache_size or config.cache_size,
    block_size_bytes = config.dcache_block_size or config.cache_block_size,
    non_cacheable_regions = config.non_cacheable_regions or {},
    ways = 4,
    mshrs = 8,
  })

  self._dmem_mux = simple.Mux("DCacheMux", { fifo_size = 32 })
  -- SM-private data map: LMEM, then identity map for on-chip GMEM / DDR.
  -- Cutover is local_mem_window (64 KiB), not sizeof usable LMEM (48 KiB).
  local lmem_window = config.local_mem_window or config.local_mem_size
  self._dmem_xbars = {}
  for i = 1, num_subcores do
    self._dmem_xbars[i] = simple.XBar("DMemXBar" .. (i - 1), 1, {
      { addr = 0x0, size = config.local_mem_size }, -- usable local memory
      {
        addr = lmem_window,
        size = (config.max_size - lmem_window + 1),
        subtract_start_addr = false,
      }, -- system map (GMEM @ 0x100000, DDR @ 0x80000000, ...)
    })
  end
  self._local_mem = BankedMemory("LocalMem", {
    size = config.local_mem_size,
    num_banks = config.num_lmem_banks or num_subcores,
    num_froms = num_subcores,
    bank_line_size = config.dcache_block_size or config.cache_block_size,
  })

  self._mux = simple.Mux("Mux", { fifo_size = 32 })

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

  self._stack_remap = simtix.StackRemapTable("StackRemapTable", {
    entries = config.stack_remap_entries,
    region_size = config.stack_size_per_thread * config.threads_per_core,
  })

  self._router = simple.XBar("SMRouter", 1, {
    { addr = config.wgi_csr_base, size = config.wgi_csr_size }, -- WGInit
    { addr = config.icache_csr_base, size = config.cache_csr_size }, -- I-Cache
    { addr = config.dcache_csr_base, size = config.cache_csr_size }, -- D-Cache
    { addr = config.core_csr_base, size = config.core_csr_size }, -- Core Info Read-only
    {
      addr = config.stack_remap_csr_base,
      size = config.stack_remap_csr_size,
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
  local scheduler = config.scheduler or "tl"
  local scheduler_config = config.scheduler_config or {}
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
      local lsu = simtix.CoalescingLsu(name, core_param, {
        cache_block_size = config.dcache_block_size or config.cache_block_size,
        enable_stack_remap = true,
        granularity = 8,
        stack_group_size = config.stack_remap_group_size,
        stack_start = 0x81000000,
        stack_end = 0x81FFFFFF,
        stack_size_per_thread = config.stack_size_per_thread,
      })
      lsu.stack_remap_table = self._stack_remap
      return lsu
    end)

    subcore:arbitrator_init(
      function(name)
        return simtix.PipelinedArbitrator(name, core_param, {
          num_read_collect_units = 4,
          num_write_collect_units = 1,
          rf_arch = "Baseline",
          num_regfile_banks = 4,
          num_subcores = pipe_param.num_subcores,
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

  -- Clock
  self._core.clock = self._clock
  self._icache.clock = self._clock
  self._dcache.clock = self._clock
  for _, dmem_xbar in ipairs(self._dmem_xbars) do
    dmem_xbar.clock = self._clock
  end
  self._local_mem.clock = self._clock
  self._router.clock = self._clock

  self._icache.target = self._mux.from
  self._dcache.target = self._mux.from

  return self
end

function PipelinedSM:set_target(target) self._mux.to = target end

function PipelinedSM:get_port() return self._router.core_side[1].port end

function PipelinedSM:__index(key)
  if key == "port" then
    return self:get_port()
  else
    return PipelinedSM[key]
  end
end

function PipelinedSM:__newindex(key, value)
  if key == "target" then
    self:set_target(value)
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
