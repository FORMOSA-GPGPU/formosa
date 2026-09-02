/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cp_hwinfo.h"

#include <stddef.h>

#include "cp_defs.h"
#include "cp_panic.h"
#include "cp_stack_remap.h"

uint64_t g_num_sm = 0;
static uint64_t g_per_sm_threads[FORMOSA_MAX_NUM_SM] = {0};
static uint64_t g_per_sm_wg_resident_limits[FORMOSA_MAX_NUM_SM] = {0};

static void validate_sm_id(size_t sm_id) {
  if (sm_id >= g_num_sm) {
    cp_panic("Invalid SM ID %lu (num_sm=%lu)", (unsigned long)sm_id,
             (unsigned long)g_num_sm);
  }
}

uint64_t cp_hwinfo_sm_wg_resident_limit(size_t sm_id) {
  validate_sm_id(sm_id);
  return g_per_sm_wg_resident_limits[sm_id];
}

uint64_t cp_hwinfo_threads_per_core(size_t sm_id) {
  validate_sm_id(sm_id);
  return g_per_sm_threads[sm_id];
}

void cp_hwinfo_init(void) {
  volatile struct system_info_mmio *system_info =
      (volatile struct system_info_mmio *)SYSTEM_INFO_BASE;

  g_num_sm = system_info->NUM_SM;
  if (g_num_sm == 0) {
    cp_panic("Invalid NUM_SM=0");
  }
  if (g_num_sm > FORMOSA_MAX_NUM_SM) {
    cp_panic("NUM_SM=%lu exceeds firmware max=%d", (unsigned long)g_num_sm,
             FORMOSA_MAX_NUM_SM);
  }

  for (size_t i = 0; i < g_num_sm; ++i) {
    volatile struct sm_mmio *sm = sm_mmio_at((unsigned)i);
    g_per_sm_threads[i] = sm->THREADS_PER_CORE;
    g_per_sm_wg_resident_limits[i] = sm->WG_RESIDENT_LIMIT;

    if (g_per_sm_threads[i] == 0) {
      cp_panic("Invalid THREADS_PER_CORE=0 for SM %lu at 0x%lx",
               (unsigned long)i, (unsigned long)&sm->THREADS_PER_CORE);
    }
    if (g_per_sm_wg_resident_limits[i] == 0) {
      cp_panic("Invalid WG_RESIDENT_LIMIT=0 for SM %lu at 0x%lx",
               (unsigned long)i, (unsigned long)&sm->WG_RESIDENT_LIMIT);
    }
    if (!cp_stack_remap_validate_config(sm)) {
      cp_panic(
          "Invalid stack remap config for SM %lu: entries=%lu "
          "threads_per_core=%lu group_size=%lu stack_base=0x%lx",
          (unsigned long)i, (unsigned long)sm->STACK_REMAP_ENTRY_COUNT,
          (unsigned long)sm->THREADS_PER_CORE,
          (unsigned long)sm->STACK_REMAP_GROUP_SIZE,
          (unsigned long)FSA_STACK_BASE);
    }
  }
}
