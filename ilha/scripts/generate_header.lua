-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

---@param output_file string
---@param source_dir string
---@param module_name? string
function main(output_file, source_dir, module_name)
  assert(output_file and source_dir, "usage: generate_header.lua <output-file> <source-dir>")
  ---@diagnostic disable-next-line: undefined-global -- provided by xmake lua.
  local address_map_module = import("ilha.lua.addr_map", { rootdir = source_dir })
  local address_map = address_map_module.xmake_address_map()
  if module_name then
    ---@diagnostic disable-next-line: undefined-global -- provided by xmake lua.
    local overrides = import(module_name, { rootdir = source_dir }).xmake_address_map()
    for key, value in pairs(overrides) do
      address_map[key] = value
    end
  end
  local function get(key)
    local value = address_map[key]
    assert(type(value) == "number", "missing numeric Ilha address-map field: " .. key)
    return value
  end

  ---@diagnostic disable-next-line: undefined-field -- xmake extends the standard os library.
  os.mkdir(output_file:match("^(.*)/[^/]+$") or ".")

  local function hex(value) return string.format("0x%Xull", value) end
  local function decimal(value, suffix) return tostring(value) .. (suffix or "") end
  local function write_file(path, contents)
    local file = assert(io.open(path, "w"))
    file:write(contents)
    file:close()
  end

  local definitions = {
    { "FSA_COMPLETION_SLOT_BYTES", decimal(get("completion_slot_bytes"), "ull") },
    { "FSA_COMPLETION_SLOT_COUNT", decimal(get("completion_slot_count"), "u") },
    { "FSA_CP_ROM_BASE", hex(get("cp_rom_base")) },
    { "FSA_CP_ROM_SIZE", hex(get("cp_rom_size")) },
    { "FSA_CP_CTRL_BASE", hex(get("cp_ctrl_base")) },
    { "FSA_CP_CTRL_SIZE", hex(get("cp_ctrl_size")) },
    { "FSA_CP_PRINTBUF_BASE", hex(get("cp_printbuf_base")) },
    { "FSA_CP_EXIT_BASE", hex(get("cp_exit_base")) },
    { "FSA_CP_PFREADER_BASE", hex(get("cp_pfreader_base")) },
    { "FSA_SYSTEM_INFO_BASE", hex(get("system_info_base")) },
    { "FSA_SYSTEM_INFO_SIZE", hex(get("system_info_size")) },
    { "FSA_SYSTEM_INFO_ABI_VERSION", decimal(get("system_info_version"), "ull") },
    { "FSA_SYSTEM_INFO_LENGTH", hex(get("system_info_length")) },
    { "FSA_SYSTEM_INFO_OFF_ABI_VERSION", hex(get("system_info_off_abi_version")) },
    { "FSA_SYSTEM_INFO_OFF_LENGTH", hex(get("system_info_off_length")) },
    { "FSA_SYSTEM_INFO_OFF_NUM_SM", hex(get("system_info_off_num_sm")) },
    { "FSA_SYSTEM_INFO_OFF_THREADS_PER_WARP", hex(get("system_info_off_threads_per_warp")) },
    { "FSA_SYSTEM_INFO_OFF_WARPS_PER_CORE", hex(get("system_info_off_warps_per_core")) },
    { "FSA_SYSTEM_INFO_OFF_LOCAL_MEM_SIZE", hex(get("system_info_off_local_mem_size")) },
    { "FSA_SYSTEM_INFO_OFF_SHARED_CACHE_SIZE", hex(get("system_info_off_shared_cache_size")) },
    { "FSA_SYSTEM_INFO_OFF_CACHE_BLOCK_SIZE", hex(get("system_info_off_cache_block_size")) },
    { "FSA_CP_KERNEL_STATE_BUF_SIZE", decimal(get("cp_kernel_state_buf_size"), "u") },
    { "FSA_HOST_DMA_CSR_BASE", hex(get("host_dma_csr_base")) },
    { "FSA_HOST_DMA_CSR_BANK", hex(get("host_dma_csr_bank")) },
    { "FSA_HOST_DMA_CSR_SIZE", hex(get("host_dma_csr_size")) },
    { "FSA_DEVICE_DMA_CSR_BASE", hex(get("device_dma_csr_base")) },
    { "FSA_DEVICE_DMA_CSR_BANK", hex(get("device_dma_csr_bank")) },
    { "FSA_DEVICE_DMA_CSR_SIZE", hex(get("device_dma_csr_size")) },
    { "FSA_CP_TCM_BASE", hex(get("cp_tcm_base")) },
    { "FSA_CP_TCM_SIZE", hex(get("cp_tcm_size")) },
    { "FSA_CLINT_BASE", hex(get("clint_base")) },
    { "FSA_CLINT_SIZE", hex(get("clint_size")) },
    { "FSA_CLINT_MSIP_OFFSET", hex(get("clint_msip_off")) },
    { "FSA_CLINT_MTIMECMP_OFFSET", hex(get("clint_mtimecmp_off")) },
    { "FSA_CLINT_MTIME_OFFSET", hex(get("clint_mtime_off")) },
    { "FSA_CLINT_MTIME_BASE", "(FSA_CLINT_BASE + FSA_CLINT_MTIME_OFFSET)" },
    { "FSA_CLINT_MTIMECMP_BASE", "(FSA_CLINT_BASE + FSA_CLINT_MTIMECMP_OFFSET)" },
    { "FSA_MMIO_BASE", hex(get("fsa_mmio_base")) },
    { "FSA_MMIO_SIZE", hex(get("mmio_size")) },
    -- Temporary shadow of the generic LV DMA register contract. Remove when LV
    -- can generate component-owned MMIO headers (issue #41).
    { "FSA_DMA_OFF_START", hex(get("dma_off_start")) },
    { "FSA_DMA_OFF_ADDR0", hex(get("dma_off_addr0")) },
    { "FSA_DMA_OFF_ADDR1", hex(get("dma_off_addr1")) },
    { "FSA_DMA_OFF_SIZE", hex(get("dma_off_size")) },
    { "FSA_DMA_OFF_STATUS", hex(get("dma_off_status")) },
    { "FSA_DMA_STATUS_IDLE", decimal(get("dma_status_idle")) },
    { "FSA_DMA_STATUS_BUSY", decimal(get("dma_status_busy")) },
    { "FSA_DMA_STATUS_DONE", decimal(get("dma_status_done")) },
    { "FSA_DMA_STATUS_BUS_ERROR", decimal(get("dma_status_bus_error")) },
    { "FSA_DMA_STATUS_INVALID_DESCRIPTOR", decimal(get("dma_status_invalid_descriptor")) },
    { "FSA_CP_CSR_BASE", hex(get("cp_csr_base")) },
    { "FSA_CP_CSR_BANK", hex(get("cp_csr_bank")) },
    { "FSA_CP_CSR_SIZE", hex(get("cp_csr_size")) },
    { "FSA_SCRATCH_BASE", hex(get("scratch_base")) },
    { "FSA_SCRATCH_SIZE", hex(get("scratch_size")) },
    { "FSA_CP_OFF_RESET", hex(get("cp_off_reset")) },
    { "FSA_CP_OFF_FW_HOST_ADDR", hex(get("cp_off_fw_host_addr")) },
    { "FSA_CP_OFF_FW_SIZE", hex(get("cp_off_fw_size")) },
    { "FSA_CP_OFF_CMD_RING_BASE", hex(get("cp_off_cmd_ring_base")) },
    { "FSA_CP_OFF_CMD_SIZE", hex(get("cp_off_cmd_size")) },
    { "FSA_CP_OFF_CMD_RING_SIZE", hex(get("cp_off_cmd_ring_size")) },
    { "FSA_CP_OFF_RD_PTR", hex(get("cp_off_rd_ptr")) },
    { "FSA_CP_OFF_WR_PTR", hex(get("cp_off_wr_ptr")) },
    { "FSA_CP_OFF_FW_STATUS", hex(get("cp_off_fw_status")) },
    { "FSA_CP_OFF_FW_ABI_VERSION", hex(get("cp_off_fw_abi_version")) },
    { "FSA_CP_OFF_FW_BOOT_GENERATION", hex(get("cp_off_fw_boot_generation")) },
    { "FSA_CP_OFF_FW_FAULT_CODE", hex(get("cp_off_fw_fault_code")) },
    { "FSA_CP_OFF_FW_ADDR", "FSA_CP_OFF_FW_HOST_ADDR" },
    { "FSA_CMD_RING_ENTRIES", hex(get("cmd_ring_entries")) },
    { "FSA_CMD_PACKET_SIZE", hex(get("cmd_packet_size")) },
    { "FSA_CMD_RING_BYTES", "(FSA_CMD_RING_ENTRIES * FSA_CMD_PACKET_SIZE)" },
    { "FSA_CMD_RING_BASE", hex(get("cmd_ring_base")) },
    { "FSA_COMPLETION_POOL_ENTRIES", hex(get("completion_pool_entries")) },
    { "FSA_COMPLETION_POOL_BASE", hex(get("completion_pool_base")) },
    { "FSA_COMPLETION_POOL_BYTES", hex(get("completion_pool_bytes")) },
    { "FSA_SM_MMIO_BASE", hex(get("sm_mmio_base")) },
    { "FSA_SM_MMIO_APERTURE", hex(get("sm_mmio_aperture")) },
    { "FSA_SM_MMIO_STRIDE", hex(get("sm_mmio_stride")) },
    { "FSA_SM_MMIO_REG_SIZE", hex(get("sm_mmio_reg_size")) },
    { "FSA_SM_MMIO_MAX_SMS", "(FSA_SM_MMIO_APERTURE / FSA_SM_MMIO_STRIDE)" },
    { "FSA_SM_WGI_CSR_OFF", hex(get("wgi_csr_base")) },
    { "FSA_SM_WGI_CSR_SIZE", hex(get("wgi_csr_size")) },
    { "FSA_SM_ICACHE_CSR_OFF", hex(get("icache_csr_base")) },
    { "FSA_SM_DCACHE_CSR_OFF", hex(get("dcache_csr_base")) },
    { "FSA_SM_CACHE_CSR_SIZE", hex(get("cache_csr_size")) },
    { "FSA_SM_CORE_CSR_OFF", hex(get("core_csr_base")) },
    { "FSA_SM_CORE_CSR_SIZE", hex(get("core_csr_size")) },
    { "FSA_SM_STACK_REMAP_CSR_OFF", hex(get("stack_remap_csr_base")) },
    { "FSA_SM_STACK_REMAP_MAX_ENTRIES", hex(get("stack_remap_max_entries")) },
    { "FSA_SM_STACK_REMAP_CSR_SIZE", hex(get("stack_remap_csr_size")) },
    { "FSA_STACK_REMAP_VALID_BIT", hex(get("stack_remap_valid_bit")) },
    { "FSA_STACK_REMAP_ADDRESS_MASK", hex(get("stack_remap_address_mask")) },
    { "FSA_ONCHIP_GMEM_BASE", hex(get("onchip_gmem_base")) },
    { "FSA_ONCHIP_GMEM_SIZE", hex(get("onchip_gmem_size")) },
    { "FSA_LMEM_BASE", hex(get("lmem_base")) },
    { "FSA_LMEM_SIZE", hex(get("lmem_size")) },
    { "FSA_LMEM_WINDOW", hex(get("lmem_window")) },
    { "FSA_SM_PRINTBUF_BASE", hex(get("sm_printbuf_base")) },
    { "FSA_SM_PRINTBUF_WINDOW", hex(get("sm_printbuf_window")) },
    { "FSA_GLOBAL_MEM_BASE", hex(get("global_mem_base")) },
    { "FSA_GLOBAL_MEM_SIZE", hex(get("global_mem_size")) },
    { "FSA_WGI_BUF_BASE", hex(get("wgi_buf_base")) },
    { "FSA_WGI_BUF_SIZE", hex(get("wgi_buf_size")) },
    { "FSA_NONCACHE_ALLOC_BASE", hex(get("noncache_alloc_base")) },
    { "FSA_NONCACHE_ALLOC_SIZE", hex(get("noncache_alloc_size")) },
    { "FSA_FIRMWARE_STAGING_BASE", hex(get("firmware_staging_base")) },
    { "FSA_FIRMWARE_STAGING_SIZE", hex(get("firmware_staging_size")) },
    { "FSA_STACK_BASE", hex(get("stack_base")) },
    { "FSA_STACK_POOL_SIZE", hex(get("stack_pool_size")) },
    { "FSA_GLOBAL_ALLOC_BASE", hex(get("global_alloc_base")) },
    { "FSA_GLOBAL_ALLOC_SIZE", hex(get("global_alloc_size")) },
    { "FSA_PER_THREAD_STACK_SIZE", hex(get("per_thread_stack_size")) },
    { "FSA_NONCACHE_REGION_BASE", hex(get("noncache_region_base")) },
    { "FSA_NONCACHE_REGION_SIZE", hex(get("noncache_region_size")) },
    { "FSA_CP_BASE", "FSA_CP_CSR_BASE" },
    { "FSA_SM0_MMIO_BASE", "FSA_SM_MMIO_BASE" },
  }

  local lines = {
    "/* Generated from ilha/lua/addr_map.lua. Do not edit. */",
    "#ifndef FORMOSA_ADDR_MAP_H",
    "#define FORMOSA_ADDR_MAP_H",
    "#ifndef __ASSEMBLER__",
    "#include <stdint.h>",
    "#endif",
    "#define FSA_ADDR_ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((a) - 1))",
  }
  for _, definition in ipairs(definitions) do
    lines[#lines + 1] = string.format("#define %-40s %s", definition[1], definition[2])
  end
  lines[#lines + 1] = "#if FSA_SM_MMIO_REG_SIZE > FSA_SM_MMIO_STRIDE"
  lines[#lines + 1] = '#error "sm_mmio does not fit in SM MMIO stride"'
  lines[#lines + 1] = "#endif"
  lines[#lines + 1] = "#if FSA_COMPLETION_POOL_BASE != (FSA_CMD_RING_BASE + FSA_CMD_RING_BYTES)"
  lines[#lines + 1] = '#error "completion pool must follow command ring"'
  lines[#lines + 1] = "#endif"
  lines[#lines + 1] = "#endif"
  write_file(output_file, table.concat(lines, "\n") .. "\n")
end
