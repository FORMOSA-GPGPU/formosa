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

static bool stack_base_is_aligned(uint64_t stack_base) {
  return (stack_base & ~FSA_STACK_REMAP_ADDRESS_MASK) == 0 &&
         (stack_base & (FSA_STACK_REMAP_REGION_SIZE - 1)) == 0;
}

bool cp_stack_remap_thread_geometry_valid(uint64_t threads_per_core) {
  return threads_per_core == FSA_STACK_REMAP_GROUP_SIZE;
}

void cp_stack_remap_reset(volatile struct sm_mmio *sm) {
  if (sm == NULL) {
    return;
  }

  for (size_t i = 0; i < FSA_SM_STACK_REMAP_ENTRIES; ++i) {
    sm->STACK_REMAP_TABLE[i] = 0;
  }
  stack_remap_write_fence();
}

int cp_stack_remap_configure(volatile struct sm_mmio *sm, uint64_t stack_base,
                             int *slot) {
  if (sm == NULL || slot == NULL || !stack_base_is_aligned(stack_base)) {
    return kCpStackRemapInvalid;
  }

  for (size_t i = 0; i < FSA_SM_STACK_REMAP_ENTRIES; ++i) {
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
  if (sm == NULL || slot < 0 || (size_t)slot >= FSA_SM_STACK_REMAP_ENTRIES) {
    return;
  }

  sm->STACK_REMAP_TABLE[slot] = 0;
  stack_remap_write_fence();
}
