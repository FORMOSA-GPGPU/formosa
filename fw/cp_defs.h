/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CP_DEFS_H
#define CP_DEFS_H

#include <formosa_addr_map.h>
#include <nanolibc/formosa/formosa.h>
#include <stddef.h>
#include <stdint.h>

#include "command_packet.h"
#include "dma.h"

// CP Parameters — bases from addr_map/formosa_addr_map.h
#ifndef CP_BASE
#define CP_BASE ((uintptr_t)FSA_CP_BASE)
#endif
#ifndef SM_MMIO_BASE
#define SM_MMIO_BASE ((uintptr_t)FSA_SM_MMIO_BASE)
#endif
#ifndef CP_WGI_BUF_BASE
#define CP_WGI_BUF_BASE ((uintptr_t)FSA_WGI_BUF_BASE)
#endif
#ifndef STACK_BASE
#define STACK_BASE ((uintptr_t)FSA_STACK_BASE)
#endif
#ifndef SM_MMIO_STRIDE
#define SM_MMIO_STRIDE ((uintptr_t)FSA_SM_MMIO_STRIDE)
#endif
#ifndef PFREADER_BASE
#define PFREADER_BASE ((uintptr_t)FSA_CP_PFREADER_BASE)
#endif

#define CP_KERNEL_STATE_BUF_SIZE 4
/* SystemInfo table lives in the CP-ROM window after the ROM payload. */
#define SYSTEM_INFO_BASE 0x210

#define CP_WGI_BUF_SIZE ((size_t)FSA_WGI_BUF_SIZE)
#define HOST_DMA_CSR_BASE ((uintptr_t)FSA_HOST_DMA_CSR_BASE)
#define DEVICE_DMA_CSR_BASE ((uintptr_t)FSA_DEVICE_DMA_CSR_BASE)

// SM Hardware Parameters
#define FORMOSA_MAX_NUM_SM 16
#define FORMOSA_MAX_WG_RESIDENT_LIMIT 8
#define PER_SM_LOCAL_MEM_BASE ((uintptr_t)FSA_LMEM_BASE)
#define PER_SM_LOCAL_MEM_SIZE ((size_t)FSA_LMEM_SIZE)

#define PER_THREAD_STACK_SIZE ((size_t)FSA_PER_THREAD_STACK_SIZE)

typedef enum { kCacheOpNop = 0, kCacheOpFlush, kCacheOpInvalidate } CacheOpMode;

_Static_assert(sizeof(union Packet) == 64,
               "Size of command packet should be 64 bytes");

struct cp_mmio {
  volatile uint64_t CP_RESET;
  volatile uint64_t CP_FW_HOST_ADDR;
  volatile uint64_t CP_FW_SIZE;
  volatile uint64_t CP_CMD_RING_BASE;
  volatile uint64_t CP_CMD_SIZE;
  volatile uint64_t CP_CMD_RING_SIZE;
  volatile uint64_t CP_RD_PTR;
  volatile uint64_t CP_WR_PTR;
  volatile uint64_t FW_STATUS;
  volatile uint64_t FW_ABI_VERSION;
  volatile uint64_t FW_BOOT_GENERATION;
  volatile uint64_t FW_FAULT_CODE;
};

_Static_assert(offsetof(struct cp_mmio, FW_STATUS) ==
                   FSA_CP_OFF_FW_STATUS - FSA_CP_OFF_RESET,
               "CP firmware status offset mismatch");
_Static_assert(offsetof(struct cp_mmio, FW_ABI_VERSION) ==
                   FSA_CP_OFF_FW_ABI_VERSION - FSA_CP_OFF_RESET,
               "CP firmware ABI offset mismatch");
_Static_assert(offsetof(struct cp_mmio, FW_BOOT_GENERATION) ==
                   FSA_CP_OFF_FW_BOOT_GENERATION - FSA_CP_OFF_RESET,
               "CP firmware boot-generation offset mismatch");
_Static_assert(offsetof(struct cp_mmio, FW_FAULT_CODE) ==
                   FSA_CP_OFF_FW_FAULT_CODE - FSA_CP_OFF_RESET,
               "CP firmware fault offset mismatch");

struct system_info_mmio {
  volatile uint64_t NUM_SM;
};

/* Shared dual-port DMA CSR (Host DMA and Device DMA). */
struct dma_mmio {
  volatile uint64_t START;
  volatile uint64_t ADDR0;
  volatile uint64_t ADDR1;
  volatile int64_t SIZE;
  volatile uint64_t STATUS;
};

_Static_assert(offsetof(struct dma_mmio, ADDR0) == FSA_DMA_OFF_ADDR0,
               "DMA ADDR0 offset mismatch");
_Static_assert(offsetof(struct dma_mmio, ADDR1) == FSA_DMA_OFF_ADDR1,
               "DMA ADDR1 offset mismatch");
_Static_assert(offsetof(struct dma_mmio, SIZE) == FSA_DMA_OFF_SIZE,
               "DMA SIZE offset mismatch");
_Static_assert(offsetof(struct dma_mmio, STATUS) == FSA_DMA_OFF_STATUS,
               "DMA STATUS offset mismatch");
_Static_assert(sizeof(struct dma_mmio) <= FSA_HOST_DMA_CSR_SIZE,
               "dma_mmio must fit in Host DMA aperture");
_Static_assert(sizeof(struct dma_mmio) <= FSA_DEVICE_DMA_CSR_SIZE,
               "dma_mmio must fit in Device DMA aperture");

_Static_assert(offsetof(struct system_info_mmio, NUM_SM) == 0x0,
               "SystemInfo.NUM_SM must be at offset 0x0");
_Static_assert(sizeof(struct system_info_mmio) == 0x8,
               "SystemInfo must be 8 bytes");

struct sm_mmio {
  // Workgroup Initializer
  //  Read/Write
  volatile uint64_t ENQ_VALID;
  volatile uint64_t ENQ_KERNEL_PC;
  volatile uint64_t ENQ_INFO_PTR;
  volatile uint64_t ENQ_NUM_THREADS;
  volatile uint64_t DEQ_VALID;
  //  Read-only
  volatile uint64_t DEQ_STATUS;
  volatile uint64_t DEQ_KERNEL_PC;
  volatile uint64_t DEQ_INFO_PTR;
  volatile uint64_t DEQ_MCAUSE;
  volatile uint64_t DEQ_MEPC;
  volatile uint64_t DEQ_MTVAL;
  volatile uint64_t WG_RESIDENT_LIMIT;
  // ICache Control
  volatile uint64_t ICACHE_CONTROL_START;
  volatile uint64_t ICACHE_CONTROL_ADDR;
  volatile uint64_t ICACHE_CONTROL_SIZE;
  volatile uint64_t ICACHE_CONTROL_MODE;
  // DCache Control
  volatile uint64_t DCACHE_CONTROL_START;
  volatile uint64_t DCACHE_CONTROL_ADDR;
  volatile uint64_t DCACHE_CONTROL_SIZE;
  volatile uint64_t DCACHE_CONTROL_MODE;
  // CoreInfo
  volatile uint64_t THREADS_PER_CORE;
  volatile uint64_t STACK_REMAP_ENTRY_COUNT;
  volatile uint64_t STACK_REMAP_GROUP_SIZE;
  // Stack remapping table entries
  // Each entry stores an aligned region base plus FSA_STACK_REMAP_VALID_BIT.
  volatile uint64_t STACK_REMAP_TABLE[FSA_SM_STACK_REMAP_MAX_ENTRIES];
};

_Static_assert(offsetof(struct sm_mmio, THREADS_PER_CORE) == 0xA0,
               "THREADS_PER_CORE must be at SM offset 0xa0");
_Static_assert(offsetof(struct sm_mmio, STACK_REMAP_ENTRY_COUNT) == 0xA8,
               "STACK_REMAP_ENTRY_COUNT must be at SM offset 0xa8");
_Static_assert(offsetof(struct sm_mmio, STACK_REMAP_GROUP_SIZE) == 0xB0,
               "STACK_REMAP_GROUP_SIZE must be at SM offset 0xb0");
_Static_assert(offsetof(struct sm_mmio, STACK_REMAP_TABLE) ==
                   FSA_SM_STACK_REMAP_CSR_OFF,
               "STACK_REMAP_TABLE offset mismatch");
_Static_assert(sizeof(struct cp_mmio) <= FSA_CP_CSR_BANK,
               "cp_mmio must fit in CP CSR bank");
_Static_assert(sizeof(struct sm_mmio) == FSA_SM_MMIO_REG_SIZE,
               "sm_mmio size must match address map");
_Static_assert(sizeof(struct sm_mmio) <= FSA_SM_MMIO_STRIDE,
               "sm_mmio must fit in per-SM MMIO stride");

/** SM[i] MMIO base — stride is 4 KiB, not sizeof(struct sm_mmio). */
static inline volatile struct sm_mmio *sm_mmio_at(unsigned sm_id) {
  return (volatile struct sm_mmio *)(SM_MMIO_BASE +
                                     (uintptr_t)sm_id * SM_MMIO_STRIDE);
}

typedef struct {
  uint64_t mcause;
  uint64_t mepc;
  uint64_t mtval;
} KernelStatus;

#endif
