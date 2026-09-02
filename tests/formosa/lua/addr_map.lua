-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

--- FORMOSA physical address map (Lua SSOT for simulation).
--- Keep numeric values in sync with addr_map/formosa_addr_map.h
---
--- @class formosa.addr_map
local M = {
  -- CP private
  cp_rom_base = 0x00000000,
  cp_rom_size = 0x00001000, -- 4 KiB window (payload may be smaller)
  cp_ctrl_base = 0x00001000,
  cp_ctrl_size = 0x00001000,
  cp_printbuf_base = 0x00001000,
  cp_exit_base = 0x00001008,
  cp_pfreader_base = 0x00001010,
  cp_tcm_base = 0x00010000,
  cp_tcm_size = 0x00040000, -- 256 KiB

  -- DMA CSRs (CP-private; after CP_CTRL, before TCM)
  host_dma_csr_base = 0x00002000,
  host_dma_csr_bank = 0x40,
  host_dma_csr_size = 0x28,
  device_dma_csr_base = 0x00002040,
  device_dma_csr_bank = 0x40,
  device_dma_csr_size = 0x28,

  -- CLINT (SiFive layout inside 64 KiB window; on fab_agent)
  clint_base = 0x00050000,
  clint_size = 0x00010000,
  clint_msip_off = 0x0,
  clint_mtimecmp_off = 0x4000,
  clint_mtime_off = 0xBFF8,

  -- Host/Agent MMIO aperture (scratch SRAM; HAL fsa_mmio_base)
  fsa_mmio_base = 0x00060000,
  mmio_size = 0x00010000,
  cp_csr_base = 0x00060100,
  cp_csr_bank = 0x100,
  cp_csr_size = 0x60,
  scratch_base = 0x00061000,
  scratch_size = 0x2000,
  cmd_ring_entries = 64,
  cmd_packet_size = 64,
  -- ABI v3: Command Ring first (0x61000..0x61FFF), Completion Pool next.
  cmd_ring_base = 0x00061000,
  completion_slot_bytes = 8,
  completion_pool_entries = 64,
  completion_pool_base = 0x00062000,
  completion_pool_bytes = 0x200,

  -- SM MMIO
  sm_mmio_base = 0x00070000,
  sm_mmio_aperture = 0x00010000,
  sm_mmio_stride = 0x1000,
  sm_mmio_reg_size = 0xF8,
  wgi_csr_base = 0x00,
  wgi_csr_size = 0x60,
  icache_csr_base = 0x60,
  dcache_csr_base = 0x80,
  cache_csr_size = 0x20,
  core_csr_base = 0xA0,
  core_csr_size = 0x18,
  stack_remap_csr_base = 0xB8,
  stack_remap_max_entries = 0x8,
  stack_remap_csr_size = 0x40,

  -- On-chip GMEM
  onchip_gmem_base = 0x00100000,
  onchip_gmem_size = 0x00080000,

  -- SM-private data map
  lmem_base = 0x0,
  lmem_size = 0xC000,
  lmem_window = 0x10000,
  sm_printbuf_base = 0x10000,
  sm_printbuf_window = 0x1000,

  -- DDR global
  global_mem_base = 0x80000000,
  global_mem_size = 0x80000000,
  wgi_buf_base = 0x80000000,
  wgi_buf_size = 0x100000,
  noncache_alloc_base = 0x80100000,
  noncache_alloc_size = 0xEC0000, -- 15 MiB - 256 KiB = 14.75 MiB
  firmware_staging_base = 0x80FC0000,
  firmware_staging_size = 0x40000,
  stack_base = 0x81000000,
  stack_pool_size = 0x1000000,
  global_alloc_base = 0x82000000,
  global_alloc_size = 0x7e000000,
  noncache_region_base = 0x80000000,
  noncache_region_size = 0x1000000,
  per_thread_stack_size = 0x400,

  max_size = 0xFFFFFFFFFFFFF,
}

--- @param sm_id integer 0-based
--- @return integer
function M.sm_mmio_addr(sm_id) return M.sm_mmio_base + sm_id * M.sm_mmio_stride end

--- Validate alignment / non-overlap invariants used by the redesigned map.
function M.validate()
  local function aligned(addr, a) return addr % a == 0 end

  assert(aligned(M.cp_rom_base, 0x1000))
  assert(aligned(M.cp_ctrl_base, 0x1000))
  assert(aligned(M.host_dma_csr_base, 0x1000))
  assert(aligned(M.cp_tcm_base, 0x1000))
  assert(aligned(M.clint_base, 0x1000))
  assert(aligned(M.fsa_mmio_base, 0x1000))
  assert(aligned(M.cmd_ring_base, 64))
  assert(M.host_dma_csr_base >= M.cp_ctrl_base + M.cp_ctrl_size)
  assert(M.device_dma_csr_base + M.device_dma_csr_size <= M.cp_tcm_base)
  assert(M.clint_base + M.clint_size == M.fsa_mmio_base)
  assert(M.cp_csr_base >= M.fsa_mmio_base)
  assert(M.scratch_base >= M.cp_csr_base + M.cp_csr_size)
  assert(M.scratch_base + M.scratch_size <= M.fsa_mmio_base + M.mmio_size)
  assert(M.stack_remap_csr_size == M.stack_remap_max_entries * 8)
  assert(M.sm_mmio_reg_size == M.stack_remap_csr_base + M.stack_remap_csr_size)
  assert(M.noncache_alloc_base + M.noncache_alloc_size == M.firmware_staging_base)
  assert(M.firmware_staging_base + M.firmware_staging_size == M.stack_base)
  assert(M.firmware_staging_size == M.cp_tcm_size)
  return true
end

M.validate()

return M
