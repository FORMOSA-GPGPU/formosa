/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CP_HWINFO_H
#define CP_HWINFO_H

#include "cp_defs.h"

extern uint64_t g_num_sm;

void cp_hwinfo_init(void);

uint64_t cp_hwinfo_sm_wg_resident_limit(size_t sm_id);
uint64_t cp_hwinfo_threads_per_core(size_t sm_id);

#endif
