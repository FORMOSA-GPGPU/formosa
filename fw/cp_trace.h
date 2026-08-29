/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CP_TRACE_H
#define CP_TRACE_H

#include "cp_defs.h"  // IWYU pragma: keep
#include "cp_hwinfo.h"
#include "cp_moving_average.h"

void cp_trace_slice_begin(int which, int val);
void cp_trace_slice_end(int which);
void cp_trace_instant(int which, int val);
void cp_trace_counter(int which, int val);

#define PF_MA_BASE 0x0
#define PF_KB_BASE (PF_MA_BASE + NUM_CP_MA_KIND)
#define PF_LMEM_BASE (PF_KB_BASE + CP_KERNEL_STATE_BUF_SIZE)
#define PF_WB_BASE (PF_LMEM_BASE + (int)g_num_sm)

static inline int cp_trace_lmem_usage_idx(size_t sm_id) {
  return PF_LMEM_BASE + (int)sm_id;
}

#ifdef FW_ENABLE_TRACE
#define DTRACE_SLICE_BEGIN(idx, val) cp_trace_slice_begin(idx, val)
#define DTRACE_SLICE_END(idx) cp_trace_slice_end(idx)
#define DTRACE_INSTANT(idx, val) cp_trace_instant(idx, val)
#define DTRACE_COUNTER(idx, val) cp_trace_counter(idx, val)
#else
#define DTRACE_SLICE_BEGIN(idx, val)
#define DTRACE_SLICE_END(idx)
#define DTRACE_INSTANT(idx, val)
#define DTRACE_COUNTER(idx, val)
#endif

#endif
