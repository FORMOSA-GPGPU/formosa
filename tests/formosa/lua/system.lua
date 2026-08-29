-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

---@class formosa.system.sm : nil
---@field port sc.Socket
---@field target sc.Socket
---@field stats stats.Group | nil

---@alias formosa.system.sm_ctor fun(name: string, config: formosa.system.config, clock: sc.clock, rst_n: sc.signal, id: number): formosa.system.sm

---@class formosa.system.opts
---@field replay boolean

---@class formosa.system
---@field protected _period sc.time
---@field protected _clock sc.clock
---@field protected _rst_n sc.signal
---@field protected _gnd sc.signal
---@field protected _cp cp.CommandProcessor
---@field protected _clint simple.Clint
---@field protected _cp_rom simple.Memory
---@field protected _cp_printbuf simple.PrintBuf
---@field protected _cp_exit_code_register simple.ExitCodeRegister
---@field protected _cp_pfreader simple.PerfettoReader
---@field protected _system_info simple.ConstantTable
---@field protected _cp_tcm simple.Memory
---@field protected _fab_cp simple.XBar
---@field protected _fab_nexus simple.XBar
---@field protected _sm formosa.system.sm[]
---@field protected _sm_printbuf simple.PrintBuf[]
---@field protected _agent ipc.Agent
---@field _replay_initiator simple.Initiator|nil
---@field protected _agent_dummy simple.Initiator|nil
---@field _replay_host_mem simple.Memory|nil
---@field protected _host_dma dma.DMA
---@field protected _device_dma dma.DMA
---@field protected _scratch simple.Memory
---@field protected _fab_agent simple.XBar
---@field protected _aligner simple.BlockAligner
---@field protected _l2cache simtix.Cache
---@field protected _l2cache_dummy_init simple.Initiator
---@field protected _gmem simple.Memory
---@field protected _dram dramsys.DRAMSys
---@field protected _fab_sys simple.XBar
---@field protected _sc_module sc.Module
---@field stats stats.Group
---@overload fun(name: string, agent_socket_path: string, config: formosa.system.config, make_sm: formosa.system.sm_ctor, opts: formosa.system.opts|nil): formosa.system
local System = {}
System.__index = System

---@param name string
---@param agent_socket_path string
---@param config formosa.system.config
---@param make_sm formosa.system.sm_ctor
---@param opts table|nil
---@return formosa.system
-- `name` is consumed by lv.sc_module.wrap to name the backing SystemC module.
function System.new(name, agent_socket_path, config, make_sm, opts)
  ---@type formosa.system
  local self = setmetatable({}, System --[[@as table]])

  local util = require("lv.util")
  self._period = sc.time(2.5, sc.time_unit.NS) -- 400 MHz
  self._clock = sc.clock("clock", self._period)
  self._rst_n = sc.signal("rst_n", true)
  self._gnd = sc.signal("gnd", false)

  -- Fixed address-map apertures (see formosa.addr_map / addr_map/formosa_addr_map.h)
  local rom_size = config.rom_size
  local tcm_start = config.tcm_start
  local tcm_size = config.tcm_size
  local clint_size = config.clint_size
  local clint_start = config.clint_base
  local host_dma_csr_base = config.host_dma_csr_base
  local host_dma_csr_size = config.host_dma_csr_size
  local device_dma_csr_base = config.device_dma_csr_base
  local device_dma_csr_size = config.device_dma_csr_size
  local cp_csr_size = config.cp_csr_size
  local gmem_base = config.gmem_base
  local gmem_size = config.gmem_size
  local scratch_pad_base = config.scratch_pad_base
  local scratch_pad_size = config.scratch_pad_size
  local cmd_ring_base = config.cmd_ring_base
  local max_size = config.max_size
  local shared_cache_size = config.shared_cache_size
  local sm_mmio_base = config.sm_mmio_base
  local sm_mmio_stride = config.sm_mmio_stride
  local printbuf_base = config.printbuf_base
  local exit_base = config.exit_base
  local pfreader_base = config.pfreader_base
  local system_info_base = config.system_info_base or 0x210
  local max_pfreader_tracks = 7
    + config.cp_kernel_state_buf_size
    + config.max_num_sm * (1 + config.wg_resident_limit)
  local pfreader_reserved_size = 0x8 * max_pfreader_tracks

  assert(config.num_sm <= config.max_num_sm)
  assert(config.num_sm * sm_mmio_stride <= config.sm_mmio_aperture)
  assert(config.max_num_sm * sm_mmio_stride <= config.sm_mmio_aperture)
  -- pfreader sits in CP_CTRL; keep reserved max size inside the aperture path to SM MMIO
  assert(pfreader_base + pfreader_reserved_size <= config.sm_mmio_base)
  assert(pfreader_base + pfreader_reserved_size <= host_dma_csr_base)
  assert(device_dma_csr_base + device_dma_csr_size <= tcm_start)
  assert(config.fsa_mmio_base + (config.mmio_size or 0x10000) <= sm_mmio_base)

  -- Unified scratch SRAM covers CP CSR through the ABI scratch region.
  local scratch_mem_base = config.fsa_mmio_base
  local scratch_mem_end = scratch_pad_base + scratch_pad_size
  local scratch_mem_size = scratch_mem_end - scratch_mem_base
  assert(config.cp_csr_base >= scratch_mem_base)
  assert(config.cp_csr_base + cp_csr_size <= scratch_mem_end)
  assert(scratch_mem_size > 0)

  -- ABI v3 scratch: command ring first at scratch base, then completion pool.
  local ring_bytes = config.cmd_ring_size * 64
  local pool_bytes = config.completion_pool_bytes
  assert(pool_bytes ~= nil and pool_bytes > 0, "completion_pool_bytes required")
  assert(config.scratch_pad_size >= ring_bytes + pool_bytes)
  assert(cmd_ring_base % 64 == 0, "cmd_ring_base must be 64-byte aligned")
  assert(cmd_ring_base == config.scratch_pad_base, "command ring must start at scratch base")
  assert(
    config.completion_pool_base == cmd_ring_base + ring_bytes,
    "completion pool must follow command ring"
  )

  -- CP subsys
  self._cp = cp.CommandProcessor("cp", 0, {
    rsp_enable = false,
    rsp_port = 9982,
  })
  self._clint = simple.Clint("clint", 1)
  self._cp_rom = simple.Memory("cp_rom", { size = rom_size })
  self._cp_printbuf = simple.PrintBuf("cp_printbuf", 1)
  self._cp_exit_code_register = simple.ExitCodeRegister("cp_exit_code_register")

  local sm_content_names = {}
  for i = 0, config.num_sm - 1 do
    table.insert(sm_content_names, string.format("SM%d", i))
  end
  local pfreader_tracks = {
    { type = 2, track_name = "ma_cmd_fetch" },
    { type = 2, track_name = "ma_kernel_state_buf_alloc" },
    { type = 2, track_name = "ma_wgi_buf_alloc" },
    { type = 2, track_name = "ma_sm_alloc" },
    { type = 2, track_name = "ma_sm_retire" },
    { type = 2, track_name = "ma_cache_start" },
    { type = 2, track_name = "ma_cache_retire" },
  }
  for i = 0, config.cp_kernel_state_buf_size - 1 do
    table.insert(pfreader_tracks, {
      type = 0,
      track_name = string.format("kernel_state_buf[%d]", i),
      content_name = { "In use" },
    })
  end
  --- Construct per sm lmem tracks
  for i = 0, config.num_sm - 1 do
    table.insert(pfreader_tracks, {
      type = 2,
      track_name = string.format("sm%d_lmem", i),
    })
  end
  --- Construct wgi_buf tracks
  local wgi_buf_count = config.num_sm * config.wg_resident_limit
  for i = 0, wgi_buf_count - 1 do
    table.insert(pfreader_tracks, {
      type = 0,
      track_name = string.format("wgi_buf[%d]", i),
      content_name = { "In use" },
    })
  end
  assert(#pfreader_tracks <= max_pfreader_tracks)

  self._cp_pfreader = simple.PerfettoReader("cp_pfreader", pfreader_tracks)
  self._system_info = simple.ConstantTable("SystemInfo", {
    entries = {
      { addr = 0x00, size = 8, value = config.num_sm }, -- Number of SMs
    },
  })
  self._cp_tcm = simple.Memory("cp_tcm", { size = tcm_size })

  -- fab_cp: CP-private regions, then one catch-all to the nexus.
  self._fab_cp = simple.XBar("fab_cp", 1, {
    { addr = 0x0, size = rom_size }, -- ROM payload
    { addr = printbuf_base, size = 0x8 }, -- printbuf
    { addr = exit_base, size = 0x8 }, -- exit code register
    { addr = system_info_base, size = 0x8 }, -- system info (NUM_SM)
    { addr = pfreader_base, size = pfreader_reserved_size }, -- pfreader (max tracks)
    { addr = host_dma_csr_base, size = host_dma_csr_size },
    { addr = device_dma_csr_base, size = device_dma_csr_size },
    { addr = tcm_start, size = tcm_size }, -- TCM
    {
      addr = clint_start,
      size = max_size - clint_start,
      subtract_start_addr = false,
    }, -- CLINT / scratch / SM / GMEM / DDR via nexus
  })

  -- fab_nexus: fixed apertures (no chain packing).
  -- Agent aperture keeps absolute addresses (subtract_start_addr=false)
  -- because fab_agent is shared with the host Agent path.
  -- SM MMIO windows subtract to a per-SM local 4 KiB view for the SM router.
  local fab_nexus_mmap = {
    {
      addr = clint_start,
      size = (config.fsa_mmio_base + (config.mmio_size or 0x10000)) - clint_start,
      subtract_start_addr = false,
    }, -- agent (CLINT + scratch)
  }
  for i = 0, config.num_sm - 1 do
    table.insert(fab_nexus_mmap, {
      addr = sm_mmio_base + i * sm_mmio_stride,
      size = sm_mmio_stride,
    })
  end
  -- System fabric (on-chip GMEM + DDR); identity addresses
  table.insert(fab_nexus_mmap, {
    addr = gmem_base,
    size = max_size,
    subtract_start_addr = false,
  })
  self._fab_nexus = simple.XBar("fab_nexus", 1, fab_nexus_mmap)

  local rom_elf = workload.ELF(util.runfile("bin/fwrom.elf"))
  self._cp_rom:load_elf(rom_elf)

  -- SM subsys
  self._sm = {}

  for i = 1, config.num_sm do
    local id = i - 1
    local sm = make_sm("SM" .. id, config, self._clock, self._rst_n, id)
    table.insert(self._sm, sm)
  end

  opts = opts or {}
  local replay = opts.replay or false

  -- Agent subsys
  self._agent = ipc.Agent("agent", {
    socket_path = agent_socket_path,
    timeout_ms = 100,
    debug = false,
  })
  self._host_dma = dma.DMA("host_dma", { fifo_size = 2 })
  self._device_dma = dma.DMA("device_dma", { fifo_size = 2 })
  local function u64_le(value)
    local bytes = {}
    for i = 1, 8 do
      bytes[i] = value % 256
      value = math.floor(value / 256)
    end
    return bytes
  end

  self._scratch = simple.Memory("scratch", { size = scratch_mem_size })
  local csr_off = config.cp_csr_base - scratch_mem_base
  self._scratch:write_bytes(csr_off + 0x18, u64_le(cmd_ring_base)) -- CMD_RING_BASE
  self._scratch:write_bytes(csr_off + 0x20, u64_le(64)) -- CMD_SIZE
  self._scratch:write_bytes(csr_off + 0x28, u64_le(config.cmd_ring_size)) -- CMD_RING_SIZE
  local fab_agent_mmap = {
    { addr = clint_start, size = clint_size }, -- CLINT (reboot + timer)
    { addr = scratch_mem_base, size = scratch_mem_size }, -- scratch (CP CSR + ABI ring)
  }
  local agent_core_ports = replay and 3 or 2
  self._fab_agent = simple.XBar("fab_agent", agent_core_ports, fab_agent_mmap)

  -- Replay-only host path. Normal capture uses ipc.Agent as the DMA host.
  if replay then
    self._replay_initiator = simple.Initiator("replay_initiator")
    self._agent_dummy = simple.Initiator("agent_dummy")
    self._replay_host_mem = simple.Memory("replay_host_mem", {
      size = opts.replay_host_mem_size or 4096,
    })
  end

  -- Global memories
  self._l2cache_dummy_init = simple.Initiator("l2cache_dummy_init")
  self._l2cache = simtix.Cache("L2Cache", {
    write_hit_policy = "WriteBack",
    size_bytes = shared_cache_size,
    block_size_bytes = config.cache_block_size,
    atomic_linearization = true,
    non_cacheable_regions = config.non_cacheable_regions or {},
    ways = 8,
    mshrs = 16,
  })
  self._aligner = simple.BlockAligner("Aligner" .. config.cache_block_size, config.cache_block_size)
  self._gmem = simple.Memory("gmem", { size = gmem_size })
  self._dram = dramsys.DRAMSys("dram", {
    config = util.runfile("tests/formosa/apccas2026-lpddr4.json"),
  })
  -- fab_sys masters: host_dma.port1, nexus, SMs, device_dma.port0, device_dma.port1
  self._fab_sys = simple.XBar("fab_sys", 4 + config.num_sm, {
    { addr = gmem_base, size = gmem_size }, -- on-chip GMEM
    { addr = config.global_mem_base, size = config.global_mem_size }, -- DDR global
  })
  -- connections
  self._cp.timer_int = self._clint.timer_irq[1]
  self._cp.sw_int = self._clint.msip_irq[1]
  self._cp.ext_int = self._gnd
  self._cp.target = self._fab_cp.core_side[1].port

  self._fab_cp.mem_side[1].target = self._cp_rom.port
  self._fab_cp.mem_side[2].target = self._cp_printbuf.port
  self._fab_cp.mem_side[3].target = self._cp_exit_code_register.port
  self._fab_cp.mem_side[4].target = self._system_info.port
  self._fab_cp.mem_side[5].target = self._cp_pfreader.slave_port
  self._fab_cp.mem_side[6].target = self._host_dma.slave_port
  self._fab_cp.mem_side[7].target = self._device_dma.slave_port
  self._fab_cp.mem_side[8].target = self._cp_tcm.port
  self._fab_cp.mem_side[9].target = self._fab_nexus.core_side[1].port

  self._cp.clock = self._clock
  self._clint.clock = self._clock
  self._cp_rom.clock = self._clock
  self._cp_printbuf.clock = self._clock
  self._cp_tcm.clock = self._clock
  self._fab_cp.clock = self._clock

  -- sm subsys connections fab_nexus
  self._fab_nexus.mem_side[1].target = self._fab_agent.core_side[2].port
  local nexus_idx = 2 -- SM MMIO starts from the number 2 index
  for i = 1, config.num_sm do
    self._fab_nexus.mem_side[nexus_idx].target = self._sm[i].port
    nexus_idx = nexus_idx + 1
  end
  self._fab_nexus.mem_side[nexus_idx].target = self._fab_sys.core_side[2].port
  self._fab_nexus.clock = self._clock

  -- sm subsys connections
  for i = 1, config.num_sm do
    self._sm[i].target = self._fab_sys.core_side[2 + i].port
  end

  -- Shared fab_agent connections
  self._agent.target = self._fab_agent.core_side[1].port
  self._host_dma.port1_target = self._fab_sys.core_side[1].port
  self._device_dma.port0_target = self._fab_sys.core_side[3 + config.num_sm].port
  self._device_dma.port1_target = self._fab_sys.core_side[4 + config.num_sm].port
  self._fab_agent.mem_side[1].target = self._clint.port
  self._fab_agent.mem_side[2].target = self._scratch.port
  self._scratch.clock = self._clock
  self._fab_agent.clock = self._clock

  -- Capture/default host path
  if not replay then self._host_dma.port0_target = self._agent.port end

  -- Replay host path
  if replay then
    self._replay_initiator.target = self._fab_agent.core_side[3].port
    self._agent_dummy.target = self._agent.port
    self._host_dma.port0_target = self._replay_host_mem.port
    self._replay_initiator.clock = self._clock
    self._agent_dummy.clock = self._clock
    self._replay_host_mem.clock = self._clock
  end

  self._fab_sys.mem_side[1].target = self._gmem.port
  self._fab_sys.mem_side[2].target = self._l2cache.port
  self._l2cache.target = self._aligner.from
  self._aligner.to = self._dram.port
  self._l2cache_dummy_init.target = self._l2cache.mmio_port
  self._gmem.clock = self._clock
  self._fab_sys.clock = self._clock
  self._l2cache.clock = self._clock
  self._l2cache_dummy_init.clock = self._clock

  self.stats = stats.Group("System")
  for _, sm in ipairs(self._sm) do
    self.stats:add_sub_group(sm.stats)
  end

  return self
end

function System:_initialize()
  self._rst_n:write(false)
  sc.start(5 * self._period)
  self._rst_n:write(true)
end

---@param cycles integer
function System:start(cycles) sc.start(cycles * self._period) end

setmetatable(System --[[@as table]], {
  __call = function(cls, ...) return cls.new(...) end,
})

return require("lv.sc_module").wrap(System, {
  -- Starting simulation completes SystemC elaboration, so it must happen
  -- after the sc.Module constructor and its hierarchy scope have returned.
  after_construct = function(system) system:_initialize() end,
})
