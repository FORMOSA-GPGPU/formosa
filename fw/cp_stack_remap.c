/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cp_stack_remap.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>

static void stack_remap_write_fence(void) {
#if defined(__riscv)
  asm volatile("fence w, w" ::: "memory");
#else
  atomic_thread_fence(memory_order_seq_cst);
#endif
}

static bool stack_remap_entry_count_valid(size_t entry_count) {
  return entry_count > 0 && entry_count <= FSA_SM_STACK_REMAP_MAX_ENTRIES;
}

static bool stack_base_is_aligned(uint64_t stack_base,
                                  uint64_t threads_per_core) {
  if (threads_per_core > UINT64_MAX / FSA_PER_THREAD_STACK_SIZE) {
    return false;
  }

  const uint64_t region_size = threads_per_core * FSA_PER_THREAD_STACK_SIZE;
  return region_size != 0 && (region_size & (region_size - 1)) == 0 &&
         (stack_base & ~FSA_STACK_REMAP_ADDRESS_MASK) == 0 &&
         (stack_base & (region_size - 1)) == 0;
}

bool cp_stack_remap_validate_config(const volatile struct sm_mmio *sm) {
  if (sm == NULL) {
    return false;
  }

  const size_t entry_count = (size_t)sm->STACK_REMAP_ENTRY_COUNT;
  const uint64_t wg_resident_limit = sm->WG_RESIDENT_LIMIT;
  const uint64_t threads_per_core = sm->THREADS_PER_CORE;
  const uint64_t group_size = sm->STACK_REMAP_GROUP_SIZE;
  return stack_remap_entry_count_valid(entry_count) &&
         entry_count >= wg_resident_limit && group_size != 0 &&
         group_size <= threads_per_core && threads_per_core % group_size == 0 &&
         stack_base_is_aligned(FSA_STACK_BASE, threads_per_core);
}

void cp_stack_remap_reset(volatile struct sm_mmio *sm) {
  if (sm == NULL) {
    return;
  }

  const size_t entry_count = (size_t)sm->STACK_REMAP_ENTRY_COUNT;
  if (!stack_remap_entry_count_valid(entry_count)) {
    return;
  }

  for (size_t i = 0; i < entry_count; ++i) {
    sm->STACK_REMAP_TABLE[i] = 0;
  }
  stack_remap_write_fence();
}

int cp_stack_remap_configure(volatile struct sm_mmio *sm, uint64_t stack_base,
                             int *slot) {
  if (sm == NULL || slot == NULL) {
    return kCpStackRemapInvalid;
  }

  const size_t entry_count = (size_t)sm->STACK_REMAP_ENTRY_COUNT;
  const uint64_t threads_per_core = sm->THREADS_PER_CORE;
  if (!stack_remap_entry_count_valid(entry_count) ||
      !stack_base_is_aligned(stack_base, threads_per_core)) {
    return kCpStackRemapInvalid;
  }

  for (size_t i = 0; i < entry_count; ++i) {
    if ((sm->STACK_REMAP_TABLE[i] & FSA_STACK_REMAP_VALID_BIT) == 0) {
      sm->STACK_REMAP_TABLE[i] = stack_base | FSA_STACK_REMAP_VALID_BIT;
      stack_remap_write_fence();
      *slot = (int)i;
      return kCpStackRemapOkay;
    }
  }

  return kCpStackRemapFull;
}

void cp_stack_remap_release(volatile struct sm_mmio *sm, int slot) {
  if (sm == NULL) {
    return;
  }

  const size_t entry_count = (size_t)sm->STACK_REMAP_ENTRY_COUNT;
  if (!stack_remap_entry_count_valid(entry_count) || slot < 0 ||
      (size_t)slot >= entry_count) {
    return;
  }

  sm->STACK_REMAP_TABLE[slot] = 0;
  stack_remap_write_fence();
}
