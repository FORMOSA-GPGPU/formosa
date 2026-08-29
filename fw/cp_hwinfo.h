/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CP_HWINFO_H
#define CP_HWINFO_H

#include "cp_defs.h"

extern uint64_t g_num_sm;
extern uint64_t g_per_sm_threads[FORMOSA_MAX_NUM_SM];
extern uint64_t g_per_sm_wg_resident_limits[FORMOSA_MAX_NUM_SM];

int cp_hwinfo_init();

static inline uint64_t cp_hwinfo_sm_wg_resident_limit(size_t sm_id) {
  return sm_id < g_num_sm ? g_per_sm_wg_resident_limits[sm_id] : 0;
}

static inline uint64_t cp_hwinfo_threads_per_core(size_t sm_id) {
  return sm_id < g_num_sm ? g_per_sm_threads[sm_id] : 0;
}

#endif
