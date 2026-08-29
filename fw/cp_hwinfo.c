/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cp_hwinfo.h"

#include <stdio.h>

#include "cp_stack_remap.h"

uint64_t g_num_sm = 0;
uint64_t g_per_sm_threads[FORMOSA_MAX_NUM_SM] = {0};
uint64_t g_per_sm_wg_resident_limits[FORMOSA_MAX_NUM_SM] = {0};

int cp_hwinfo_init() {
  volatile struct system_info_mmio *system_info =
      (volatile struct system_info_mmio *)SYSTEM_INFO_BASE;

  g_num_sm = system_info->NUM_SM;
  if (g_num_sm == 0) {
    fprintf(stderr, "\033[31mInvalid NUM_SM: 0\033[0m\r\n");
    return -1;
  }
  if (g_num_sm > FORMOSA_MAX_NUM_SM) {
    fprintf(stderr,
            "\033[31mNUM_SM=%lu exceeds FORMOSA_MAX_NUM_SM=%d\033[0m\r\n",
            (unsigned long)g_num_sm, FORMOSA_MAX_NUM_SM);
    return -1;
  }

  for (size_t i = 0; i < g_num_sm; ++i) {
    volatile struct sm_mmio *sm = sm_mmio_at((unsigned)i);
    g_per_sm_threads[i] = sm->THREADS_PER_CORE;
    if (g_per_sm_threads[i] == 0) {
      fprintf(stderr,
              "\033[31mInvalid THREADS_PER_CORE=0 for SM %lu at "
              "0x%lx\033[0m\r\n",
              (unsigned long)i, (unsigned long)&sm->THREADS_PER_CORE);
      return -1;
    }
    if (!cp_stack_remap_thread_geometry_valid(g_per_sm_threads[i])) {
      fprintf(stderr,
              "\033[31mTHREADS_PER_CORE=%lu for SM %lu is incompatible with "
              "STACK_REMAP_GROUP_SIZE=%lu\033[0m\r\n",
              (unsigned long)g_per_sm_threads[i], (unsigned long)i,
              (unsigned long)FSA_STACK_REMAP_GROUP_SIZE);
      return -1;
    }
    g_per_sm_wg_resident_limits[i] = sm->WG_RESIDENT_LIMIT;
    if (g_per_sm_wg_resident_limits[i] == 0) {
      fprintf(stderr,
              "\033[31mInvalid WG_RESIDENT_LIMIT=0 for SM %lu at "
              "0x%lx\033[0m\r\n",
              (unsigned long)i, (unsigned long)&sm->WG_RESIDENT_LIMIT);
      return -1;
    }
  }
  return 0;
}
