/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * FORMOSA physical address map (single source of truth for C/asm).
 *
 * Design rules:
 * - Region bases are fixed apertures (no chain packing).
 * - Windows are power-of-two aligned (4 KiB / 64 KiB / 1 MiB).
 * - Changing a region's *size* must not move other region bases.
 * - Keep in sync with tests/formosa/lua/addr_map.lua
 *
 * Layout (simulation):
 *
 *   0x0000_0000  CP_ROM          4 KiB     (fab_cp)
 *   0x0000_1000  CP_CTRL         4 KiB     (fab_cp; printbuf / exit / pfreader)
 *   0x0000_2000  DMA CSRs                  (fab_cp; Host DMA + Device DMA)
 *   0x0001_0000  CP_TCM        256 KiB     (fab_cp)
 *   0x0005_0000  CLINT          64 KiB     (fab_agent; SiFive offsets inside)
 *   0x0006_0000  MMIO           64 KiB     (fab_agent; scratch SRAM)
 *   0x0007_0000  SM_MMIO        64 KiB     (SM[i] @ +i*4KiB)
 *   0x0010_0000  ONCHIP_GMEM   512 KiB
 *   0x8000_0000  DDR_GLOBAL       2 GiB window
 *
 * HAL fsa_mmio_base is FSA_MMIO_BASE (start of fab_agent scratch), not the DMA
 * CSRs. Command ring / completion pool live at FSA_SCRATCH_BASE inside MMIO.
 */

#ifndef FORMOSA_ADDR_MAP_H
#define FORMOSA_ADDR_MAP_H

#ifndef __ASSEMBLER__
#include <stdint.h>
#endif

/* Fixed ABI v3 constants shared with command_packet.h. */
#define FSA_COMPLETION_SLOT_BYTES 8ull
#define FSA_COMPLETION_SLOT_COUNT 64u

/* ---- Helpers ---- */
#define FSA_ADDR_ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((a) - 1))

/* ---- CP private fabric ---- */
#define FSA_CP_ROM_BASE 0x00000000ull
#define FSA_CP_ROM_SIZE 0x00001000ull /* 4 KiB window */

#define FSA_CP_CTRL_BASE 0x00001000ull
#define FSA_CP_CTRL_SIZE 0x00001000ull /* 4 KiB window */
#define FSA_CP_PRINTBUF_BASE (FSA_CP_CTRL_BASE + 0x0ull)
#define FSA_CP_EXIT_BASE (FSA_CP_CTRL_BASE + 0x8ull)
#define FSA_CP_PFREADER_BASE (FSA_CP_CTRL_BASE + 0x10ull)

/* DMA CSR banks (CP-private; after CP_CTRL, before TCM) */
#define FSA_HOST_DMA_CSR_BASE 0x00002000ull
#define FSA_HOST_DMA_CSR_BANK 0x40ull
#define FSA_HOST_DMA_CSR_SIZE 0x28ull
#define FSA_DEVICE_DMA_CSR_BASE (FSA_HOST_DMA_CSR_BASE + FSA_HOST_DMA_CSR_BANK)
#define FSA_DEVICE_DMA_CSR_BANK 0x40ull
#define FSA_DEVICE_DMA_CSR_SIZE 0x28ull

#define FSA_CP_TCM_BASE 0x00010000ull
#define FSA_CP_TCM_SIZE 0x00040000ull /* 256 KiB */

/* SiFive-compatible CLINT window (mtime @ +0xBFF8, mtimecmp hart0 @ +0x4000) */
#define FSA_CLINT_BASE 0x00050000ull
#define FSA_CLINT_SIZE 0x00010000ull /* 64 KiB window */
#define FSA_CLINT_MSIP_OFFSET 0x0ull
#define FSA_CLINT_MTIMECMP_OFFSET 0x4000ull
#define FSA_CLINT_MTIME_OFFSET 0xBFF8ull
#define FSA_CLINT_MTIME_BASE (FSA_CLINT_BASE + FSA_CLINT_MTIME_OFFSET)
#define FSA_CLINT_MTIMECMP_BASE (FSA_CLINT_BASE + FSA_CLINT_MTIMECMP_OFFSET)

/* ---- Host/Agent MMIO aperture (HAL fsa_mmio_base) ---- */
#define FSA_MMIO_BASE 0x00060000ull
#define FSA_MMIO_SIZE 0x00010000ull /* 64 KiB */

/* Shared dual-port DMA CSR offsets */
#define FSA_DMA_OFF_START 0x00ull
#define FSA_DMA_OFF_ADDR0 0x08ull
#define FSA_DMA_OFF_ADDR1 0x10ull
#define FSA_DMA_OFF_SIZE 0x18ull
#define FSA_DMA_OFF_STATUS 0x20ull

#define FSA_CP_CSR_BASE (FSA_MMIO_BASE + 0x0100ull)
#define FSA_CP_CSR_BANK 0x100ull
#define FSA_CP_CSR_SIZE 0x60ull /* sizeof(struct cp_mmio) */

/* ABI scratch region inside the MMIO SRAM (cmd ring + completion pool). */
#define FSA_SCRATCH_BASE (FSA_MMIO_BASE + 0x1000ull)
#define FSA_SCRATCH_SIZE 0x2000ull /* cmd ring + completion pool + reserved */

/* Boot Descriptor + command-ring CSRs (relative to FSA_MMIO_BASE). */
#define FSA_CP_OFF_RESET (0x100ull + 0x00ull)
#define FSA_CP_OFF_FW_HOST_ADDR (0x100ull + 0x08ull)
#define FSA_CP_OFF_FW_SIZE (0x100ull + 0x10ull)
#define FSA_CP_OFF_CMD_RING_BASE (0x100ull + 0x18ull)
#define FSA_CP_OFF_CMD_SIZE (0x100ull + 0x20ull)
#define FSA_CP_OFF_CMD_RING_SIZE (0x100ull + 0x28ull)
#define FSA_CP_OFF_RD_PTR (0x100ull + 0x30ull)
#define FSA_CP_OFF_WR_PTR (0x100ull + 0x38ull)
#define FSA_CP_OFF_FW_STATUS (0x100ull + 0x40ull)
#define FSA_CP_OFF_FW_ABI_VERSION (0x100ull + 0x48ull)
#define FSA_CP_OFF_FW_BOOT_GENERATION (0x100ull + 0x50ull)
#define FSA_CP_OFF_FW_FAULT_CODE (0x100ull + 0x58ull)

/* Compatibility aliases for existing callers. */
#define FSA_CP_OFF_FW_ADDR FSA_CP_OFF_FW_HOST_ADDR

/* ABI v3 scratch: Command Ring first, then shared Completion Pool, rest
 * reserved. Ring 0x61000..0x61FFF, pool 0x62000..0x621FF when MMIO@0x60000. */
#define FSA_CMD_RING_ENTRIES 64ull
#define FSA_CMD_PACKET_SIZE 64ull
#define FSA_CMD_RING_BYTES \
  (FSA_CMD_RING_ENTRIES * FSA_CMD_PACKET_SIZE) /* 0x1000 */
#define FSA_CMD_RING_BASE FSA_SCRATCH_BASE
/* Pool capacity matches the command ring depth; ABI token index space is
 * FSA_COMPLETION_SLOT_COUNT (must stay equal — checked where both headers are
 * visible). */
#define FSA_COMPLETION_POOL_ENTRIES FSA_CMD_RING_ENTRIES
#define FSA_COMPLETION_POOL_BASE (FSA_CMD_RING_BASE + FSA_CMD_RING_BYTES)
#define FSA_COMPLETION_POOL_BYTES \
  (FSA_COMPLETION_POOL_ENTRIES * FSA_COMPLETION_SLOT_BYTES) /* 0x200 */

/* ---- SM MMIO aperture ---- */
#define FSA_SM_MMIO_BASE 0x00070000ull
#define FSA_SM_MMIO_APERTURE 0x00010000ull /* 64 KiB total */
#define FSA_SM_MMIO_STRIDE 0x1000ull       /* 4 KiB per SM */
#define FSA_SM_MMIO_REG_SIZE 0xE8ull       /* sizeof(struct sm_mmio) */
#define FSA_SM_MMIO_MAX_SMS (FSA_SM_MMIO_APERTURE / FSA_SM_MMIO_STRIDE)

/* Per-SM CSR offsets inside the 4 KiB window (matches struct sm_mmio) */
#define FSA_SM_WGI_CSR_OFF 0x00ull
#define FSA_SM_WGI_CSR_SIZE 0x60ull
#define FSA_SM_ICACHE_CSR_OFF 0x60ull
#define FSA_SM_DCACHE_CSR_OFF 0x80ull
#define FSA_SM_CACHE_CSR_SIZE 0x20ull
#define FSA_SM_CORE_CSR_OFF 0xA0ull
#define FSA_SM_CORE_CSR_SIZE 0x08ull
#define FSA_SM_STACK_REMAP_CSR_OFF 0xA8ull
#define FSA_SM_STACK_REMAP_ENTRIES 8ull
#define FSA_SM_STACK_REMAP_CSR_SIZE (FSA_SM_STACK_REMAP_ENTRIES * 8ull)
#define FSA_STACK_REMAP_VALID_BIT 0x1ull
#define FSA_STACK_REMAP_ADDRESS_MASK 0x0000FFFFFFFFFFFFull
#define FSA_STACK_REMAP_GROUP_SIZE 64ull
#define FSA_STACK_REMAP_REGION_SIZE \
  (FSA_STACK_REMAP_GROUP_SIZE * FSA_PER_THREAD_STACK_SIZE)

/* ---- On-chip global SRAM (system fabric) ---- */
#define FSA_ONCHIP_GMEM_BASE 0x00100000ull
#define FSA_ONCHIP_GMEM_SIZE 0x00080000ull /* 512 KiB */

/* ---- SM-private data map (core load/store view) ---- */
#define FSA_LMEM_BASE 0x00000000ull
#define FSA_LMEM_SIZE 0x0000C000ull   /* usable local memory */
#define FSA_LMEM_WINDOW 0x00010000ull /* decode window (64 KiB) */
#define FSA_SM_PRINTBUF_BASE 0x00010000ull
#define FSA_SM_PRINTBUF_WINDOW 0x00001000ull

/* ---- DDR / device global ---- */
#define FSA_GLOBAL_MEM_BASE 0x80000000ull
#define FSA_GLOBAL_MEM_SIZE 0x80000000ull /* 2 GiB window */

#define FSA_WGI_BUF_BASE FSA_GLOBAL_MEM_BASE
#define FSA_WGI_BUF_SIZE 0x00100000ull /* 1 MiB */

/* Non-cache heap shrinks by 256 KiB to reserve firmware staging. */
#define FSA_NONCACHE_ALLOC_BASE (FSA_WGI_BUF_BASE + FSA_WGI_BUF_SIZE)
#define FSA_NONCACHE_ALLOC_SIZE 0x00EC0000ull /* 15 MiB - 256 KiB */

/* Fixed firmware staging aperture (Host DMA destination; not runtime-copyable).
 */
#define FSA_FIRMWARE_STAGING_BASE 0x80FC0000ull
#define FSA_FIRMWARE_STAGING_SIZE 0x00040000ull /* = CP_TCM_SIZE */

#define FSA_STACK_BASE 0x81000000ull
#define FSA_STACK_POOL_SIZE 0x01000000ull /* 16 MiB reserved */

#define FSA_GLOBAL_ALLOC_BASE 0x82000000ull
#define FSA_GLOBAL_ALLOC_SIZE \
  (FSA_GLOBAL_MEM_BASE + FSA_GLOBAL_MEM_SIZE - FSA_GLOBAL_ALLOC_BASE)
#define FSA_PER_THREAD_STACK_SIZE 0x400ull

/* Non-cacheable DDR prefix: WGI + noncache heap + staging (+ up to stack base)
 */
#define FSA_NONCACHE_REGION_BASE FSA_GLOBAL_MEM_BASE
#define FSA_NONCACHE_REGION_SIZE 0x01000000ull /* 16 MiB */

/* ---- Convenience aliases used by firmware ---- */
#define FSA_CP_BASE FSA_CP_CSR_BASE
#define FSA_SM0_MMIO_BASE FSA_SM_MMIO_BASE

/* Compile-time sanity */
#if (FSA_CMD_RING_BASE % 64ull) != 0ull
#error "FSA_CMD_RING_BASE must be 64-byte aligned"
#endif
#if (FSA_SM_MMIO_REG_SIZE) > (FSA_SM_MMIO_STRIDE)
#error "sm_mmio does not fit in SM MMIO stride"
#endif
#if (FSA_CP_CSR_SIZE) > (FSA_CP_CSR_BANK)
#error "cp_mmio does not fit in CP CSR bank"
#endif
#if (FSA_HOST_DMA_CSR_SIZE) > (FSA_HOST_DMA_CSR_BANK)
#error "host DMA CSR does not fit in its bank"
#endif
#if (FSA_DEVICE_DMA_CSR_SIZE) > (FSA_DEVICE_DMA_CSR_BANK)
#error "device DMA CSR does not fit in its bank"
#endif
#if (FSA_HOST_DMA_CSR_BASE) < (FSA_CP_CTRL_BASE + FSA_CP_CTRL_SIZE)
#error "host DMA CSR overlaps CP_CTRL"
#endif
#if (FSA_DEVICE_DMA_CSR_BASE + FSA_DEVICE_DMA_CSR_SIZE) > (FSA_CP_TCM_BASE)
#error "device DMA CSR overlaps CP TCM"
#endif
#if (FSA_CLINT_BASE + FSA_CLINT_SIZE) != (FSA_MMIO_BASE)
#error "CLINT must sit immediately before MMIO on fab_agent"
#endif
#if (FSA_FIRMWARE_STAGING_SIZE) != (FSA_CP_TCM_SIZE)
#error "firmware staging size must match CP TCM size"
#endif
#if (FSA_NONCACHE_ALLOC_BASE + FSA_NONCACHE_ALLOC_SIZE) != \
    (FSA_FIRMWARE_STAGING_BASE)
#error "noncache heap must end at firmware staging base"
#endif
#if (FSA_FIRMWARE_STAGING_BASE + FSA_FIRMWARE_STAGING_SIZE) != (FSA_STACK_BASE)
#error "firmware staging must end at stack base"
#endif
#if (FSA_CMD_RING_BASE + FSA_CMD_RING_BYTES + FSA_COMPLETION_POOL_BYTES) > \
    (FSA_SCRATCH_BASE + FSA_SCRATCH_SIZE)
#error "scratch pad too small for ABI v3 cmd ring + completion pool"
#endif
#if (FSA_CMD_RING_BASE) != (FSA_SCRATCH_BASE)
#error "ABI v3 Command Ring must start at scratch base"
#endif
#if (FSA_COMPLETION_POOL_BASE) != (FSA_CMD_RING_BASE + FSA_CMD_RING_BYTES)
#error "ABI v3 Completion Pool must follow Command Ring"
#endif

#endif /* FORMOSA_ADDR_MAP_H */
