-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

--- FORMOSA physical address map and software-generation SSOT.
---@class ilha.addr_map
local AddressMap = {
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
  -- SystemInfo ABI v1: individually readable unsigned 64-bit little-endian fields.
  system_info_base = 0x00060000,
  system_info_size = 0x100,
  system_info_version = 1,
  system_info_length = 0x40,
  system_info_off_abi_version = 0x00,
  system_info_off_length = 0x08,
  system_info_off_num_sm = 0x10,
  system_info_off_threads_per_warp = 0x18,
  system_info_off_warps_per_core = 0x20,
  system_info_off_local_mem_size = 0x28,
  system_info_off_shared_cache_size = 0x30,
  system_info_off_cache_block_size = 0x38,
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
  completion_slot_count = 64,
  completion_pool_entries = 64,
  completion_pool_base = 0x00062000,
  completion_pool_bytes = 0x200,
  cp_kernel_state_buf_size = 4,

  -- CP register ABI
  cp_off_reset = 0x100,
  cp_off_fw_host_addr = 0x108,
  cp_off_fw_size = 0x110,
  cp_off_cmd_ring_base = 0x118,
  cp_off_cmd_size = 0x120,
  cp_off_cmd_ring_size = 0x128,
  cp_off_rd_ptr = 0x130,
  cp_off_wr_ptr = 0x138,
  cp_off_fw_status = 0x140,
  cp_off_fw_abi_version = 0x148,
  cp_off_fw_boot_generation = 0x150,
  cp_off_fw_fault_code = 0x158,

  -- Temporary shadow of LV's generic DMA register contract (issue #41).
  dma_off_start = 0x00,
  dma_off_addr0 = 0x08,
  dma_off_addr1 = 0x10,
  dma_off_size = 0x18,
  dma_off_status = 0x20,
  dma_status_idle = 0,
  dma_status_busy = 1,
  dma_status_done = 2,
  dma_status_bus_error = 3,
  dma_status_invalid_descriptor = 4,

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
  stack_remap_valid_bit = 0x1,
  stack_remap_address_mask = 0x0000FFFFFFFFFFFF,

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
function AddressMap.sm_mmio_addr(sm_id)
  return AddressMap.sm_mmio_base + sm_id * AddressMap.sm_mmio_stride
end

--- Validate alignment / non-overlap invariants used by the redesigned map.
function AddressMap.validate()
  local function aligned(addr, a) return addr % a == 0 end

  assert(aligned(AddressMap.cp_rom_base, 0x1000))
  assert(aligned(AddressMap.cp_ctrl_base, 0x1000))
  assert(aligned(AddressMap.host_dma_csr_base, 0x1000))
  assert(aligned(AddressMap.cp_tcm_base, 0x1000))
  assert(aligned(AddressMap.clint_base, 0x1000))
  assert(aligned(AddressMap.fsa_mmio_base, 0x1000))
  assert(aligned(AddressMap.cmd_ring_base, 64))
  assert(AddressMap.host_dma_csr_base >= AddressMap.cp_ctrl_base + AddressMap.cp_ctrl_size)
  assert(AddressMap.device_dma_csr_base + AddressMap.device_dma_csr_size <= AddressMap.cp_tcm_base)
  assert(AddressMap.clint_base + AddressMap.clint_size == AddressMap.fsa_mmio_base)
  assert(AddressMap.cp_csr_base >= AddressMap.fsa_mmio_base)
  assert(AddressMap.system_info_base == AddressMap.fsa_mmio_base)
  assert(AddressMap.system_info_length <= AddressMap.system_info_size)
  assert(AddressMap.system_info_base + AddressMap.system_info_size <= AddressMap.cp_csr_base)
  assert(AddressMap.scratch_base >= AddressMap.cp_csr_base + AddressMap.cp_csr_size)
  assert(
    AddressMap.scratch_base + AddressMap.scratch_size
      <= AddressMap.fsa_mmio_base + AddressMap.mmio_size
  )
  assert(AddressMap.stack_remap_csr_size == AddressMap.stack_remap_max_entries * 8)
  assert(
    AddressMap.sm_mmio_reg_size == AddressMap.stack_remap_csr_base + AddressMap.stack_remap_csr_size
  )
  assert(
    AddressMap.noncache_alloc_base + AddressMap.noncache_alloc_size
      == AddressMap.firmware_staging_base
  )
  assert(
    AddressMap.firmware_staging_base + AddressMap.firmware_staging_size == AddressMap.stack_base
  )
  assert(AddressMap.firmware_staging_size == AddressMap.cp_tcm_size)
  return true
end

AddressMap.validate()

-- xmake's sandboxed module loader does not preserve a module's return value.
-- Export a loader only inside that sandbox so the generator executes this SSOT
-- instead of parsing its source text.
---@diagnostic disable-next-line: undefined-global
if type(import) == "function" then
  function xmake_address_map() return AddressMap end
end

return AddressMap
