/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CP_CONTEXT_H
#define CP_CONTEXT_H

#include <stdint.h>

#include "cp_defs.h"

struct cp_kernel_state {
  FsaCompletionToken completion_token;
  volatile uint32_t in_use;
  uint32_t wg_left;
};

struct cp_cache_state {
  // There are undergoing operation
  volatile uint32_t pending;
  // One bit per SM to indicate that the cache operation for it is pending
  volatile uint64_t pending_mask;
  // The mode that the cache is currently handling
  CacheOpMode mode;
  /* Host token, or 0 for firmware-internal cache ops. */
  FsaCompletionToken completion_token;
};

enum cp_memory_copy_phase {
  kMemoryCopyPhaseIdle = 0,
  kMemoryCopyPhaseWaitPredecessor,
  kMemoryCopyPhaseFlushSource,
  kMemoryCopyPhaseFlushDestination,
  kMemoryCopyPhaseWaitDma,
  kMemoryCopyPhaseInvalidateDestination,
  kMemoryCopyPhaseDrainDma,
};

struct cp_memory_copy_state {
  volatile uint32_t active;
  enum cp_memory_copy_phase phase;
  MemoryDomain src_domain;
  MemoryDomain dst_domain;
  uint64_t src_addr;
  uint64_t dst_addr;
  uint64_t size;
  FsaCompletionToken completion_token;
  uint32_t phase_started_tick;
};

extern struct cp_kernel_state kernel_state_buf[];
extern const int kernel_state_buf_size;

extern struct cp_cache_state cache_state;
extern struct cp_memory_copy_state memory_copy_state;
/* Return id if success, -1 if no free buf */
int cp_kernel_state_buf_alloc();
void cp_kernel_state_buf_free(int id);

#endif
