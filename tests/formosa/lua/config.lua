-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local stdlib = require("posix.stdlib")
local addr = require("formosa.addr_map")

local prefix = "LV_FORMOSA_"
local max_num_sm = 16

-- Per SM hardware configurations
local threads_per_warp = 4
local warps_per_core = 16
local threads_per_core = threads_per_warp * warps_per_core
local wg_resident_limit = 8 -- Per SM resident work-group limit
local cp_kernel_state_buf_size = 4
local num_sm = 1
assert(
  num_sm and num_sm >= 1 and num_sm <= max_num_sm and num_sm % 1 == 0,
  string.format("num_sm must be an integer between 1 and %d", max_num_sm)
)
assert(
  max_num_sm * addr.sm_mmio_stride <= addr.sm_mmio_aperture,
  "SM MMIO aperture must fit FORMOSA_MAX_NUM_SM windows"
)

-- sm_mmio_reg_size must match sizeof(struct sm_mmio) in fw/cp_defs.h.
-- Decode window per SM is sm_mmio_stride (4 KiB), independent of reg size.
assert(addr.sm_mmio_reg_size == addr.stack_remap_csr_base + addr.stack_remap_csr_size)

---@class formosa.system.config
local Config = {
  -- Address map (fixed apertures; see formosa.addr_map / addr_map/formosa_addr_map.h)
  rom_size = 0x200, -- payload size inside CP_ROM window (below system_info @ 0x210)
  rom_window = addr.cp_rom_size,
  tcm_start = addr.cp_tcm_base,
  tcm_size = addr.cp_tcm_size,
  clint_size = addr.clint_size,
  clint_base = addr.clint_base,
  -- HAL fsa_mmio_base: fab_agent scratch (MMIO), not CP-private DMA CSRs
  fsa_mmio_base = addr.fsa_mmio_base,
  mmio_size = addr.mmio_size,
  host_dma_csr_base = addr.host_dma_csr_base,
  host_dma_csr_size = addr.host_dma_csr_size,
  host_dma_csr_bank = addr.host_dma_csr_bank,
  device_dma_csr_base = addr.device_dma_csr_base,
  device_dma_csr_size = addr.device_dma_csr_size,
  device_dma_csr_bank = addr.device_dma_csr_bank,
  firmware_staging_base = addr.firmware_staging_base,
  firmware_staging_size = addr.firmware_staging_size,
  cp_csr_size = addr.cp_csr_size,
  cp_csr_bank = addr.cp_csr_bank,
  cp_csr_base = addr.cp_csr_base,
  scratch_pad_base = addr.scratch_base,
  scratch_pad_size = addr.scratch_size,
  cmd_ring_size = addr.cmd_ring_entries,
  cmd_ring_base = addr.cmd_ring_base,
  completion_pool_base = addr.completion_pool_base,
  completion_pool_entries = addr.completion_pool_entries,
  completion_pool_bytes = addr.completion_pool_bytes,
  sm_mmio_base = addr.sm_mmio_base,
  sm_mmio_stride = addr.sm_mmio_stride,
  sm_mmio_size = addr.sm_mmio_reg_size, -- struct size (legacy name)
  sm_mmio_aperture = addr.sm_mmio_aperture,
  printbuf_base = addr.cp_printbuf_base,
  exit_base = addr.cp_exit_base,
  pfreader_base = addr.cp_pfreader_base,
  system_info_base = 0x210, -- CP-ROM window after ROM payload (fw SYSTEM_INFO_BASE)
  gmem_base = addr.onchip_gmem_base,
  gmem_size = addr.onchip_gmem_size,
  local_mem_size = addr.lmem_size,
  local_mem_window = addr.lmem_window,
  sm_printbuf_base = addr.sm_printbuf_base,
  global_mem_base = addr.global_mem_base,
  global_mem_alloc_base = addr.global_alloc_base,
  global_mem_size = addr.global_mem_size,
  global_mem_noncache_alloc_base = addr.noncache_alloc_base,
  global_mem_noncache_alloc_size = addr.noncache_alloc_size,
  stack_base = addr.stack_base,
  max_size = addr.max_size,

  -- SM CSR layout (relative to each SM MMIO window)
  wgi_csr_size = addr.wgi_csr_size,
  cache_csr_size = addr.cache_csr_size,
  core_csr_size = addr.core_csr_size,
  wgi_csr_base = addr.wgi_csr_base,
  icache_csr_base = addr.icache_csr_base,
  dcache_csr_base = addr.dcache_csr_base,
  core_csr_base = addr.core_csr_base,
  stack_remap_csr_base = addr.stack_remap_csr_base,
  stack_remap_csr_size = addr.stack_remap_csr_size,
  stack_remap_entries = addr.stack_remap_entries,
  stack_remap_group_size = addr.stack_remap_group_size,

  -- Caches / cores
  cache_size = 0x1000,
  icache_size = 0x1000, -- 4KB
  dcache_size = 0x4000, -- 16KB
  cache_block_size = 64,
  shared_cache_size = 0x20000,
  stack_size_per_thread = addr.per_thread_stack_size,
  num_sm = num_sm,
  max_num_sm = max_num_sm,
  threads_per_warp = threads_per_warp,
  warps_per_core = warps_per_core,
  threads_per_core = threads_per_core,
  wg_resident_limit = wg_resident_limit,
  cp_kernel_state_buf_size = cp_kernel_state_buf_size,

  non_cacheable_regions = {
    { addr = addr.sm_printbuf_base, size = threads_per_core },
    -- WGI + noncache heap (HAL: 0x80000000..0x81000000)
    { addr = addr.noncache_region_base, size = addr.noncache_region_size },
  },
}

function Config:foreach(cb)
  for k, v in pairs(self) do
    local v_type = type(v)
    if v_type == "string" or v_type == "number" or v_type == "boolean" then
      local var = prefix .. string.upper(k)
      local val = tostring(v)
      cb(var, val)
    end
  end
end

function Config:dump(filename)
  local env_file = assert(io.open(filename, "w"), "Error: Could not open env_file for writing")
  self:foreach(function(var, val) env_file:write(string.format("export %s=%s\n", var, val)) end)
  env_file:write("\n")
  env_file:flush()
  env_file:close()
end

function Config:export()
  self:foreach(function(var, val) stdlib.setenv(var, val) end)
end

return Config
