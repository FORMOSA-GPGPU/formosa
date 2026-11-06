/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cp_wgi_buffer.h"

#include <stdbool.h>
#include <stdio.h>

#include "cp_defs.h"
#include "cp_hwinfo.h"
#include "cp_trace.h"

struct cp_wgi_buf *const wgi_buf = (struct cp_wgi_buf *)CP_WGI_BUF_BASE;
size_t wgi_buf_len = 0;

int cp_wgi_buf_init() {
  const size_t capacity = CP_WGI_BUF_SIZE / sizeof(struct cp_wgi_buf);

  if (g_num_sm == 0 || g_num_sm > FORMOSA_MAX_NUM_SM) {
    fprintf(stderr,
            "\033[31mCannot initialize WGI buffer with NUM_SM=%lu\033[0m\r\n",
            (unsigned long)g_num_sm);
    return -1;
  }

  wgi_buf_len = 0;
  for (size_t i = 0; i < g_num_sm; ++i) {
    const uint64_t limit = cp_hwinfo_sm_wg_resident_limit(i);
    if (limit == 0 || limit > capacity - wgi_buf_len) {
      fprintf(stderr,
              "\033[31mInvalid WG_RESIDENT_LIMIT=%lu for SM %lu: "
              "WGI slots %lu + %lu exceed capacity %lu\033[0m\r\n",
              (unsigned long)limit, (unsigned long)i,
              (unsigned long)wgi_buf_len, (unsigned long)limit,
              (unsigned long)capacity);
      return -1;
    }
    wgi_buf_len += limit;
  }

  /* Dispatch gives slot i the stacks at STACK_BASE + i * threads_per_core *
   * PER_THREAD_STACK_SIZE for the SM the work-group runs on, so every slot's
   * stacks must end below the global allocations on every SM. */
  const uint64_t threads_per_slot =
      (cp_hwinfo_global_alloc_base() - STACK_BASE) / PER_THREAD_STACK_SIZE /
      wgi_buf_len;
  for (size_t i = 0; i < g_num_sm; ++i) {
    if (cp_hwinfo_threads_per_core(i) > threads_per_slot) {
      fprintf(stderr,
              "\033[31mStacks of %lu WGI slots with THREADS_PER_CORE=%lu for "
              "SM %lu cross GLOBAL_ALLOC_BASE=0x%lx\033[0m\r\n",
              (unsigned long)wgi_buf_len,
              (unsigned long)cp_hwinfo_threads_per_core(i), (unsigned long)i,
              (unsigned long)cp_hwinfo_global_alloc_base());
      return -1;
    }
  }

  for (size_t i = 0; i < wgi_buf_len; i++) {
    wgi_buf[i].in_use = 0;
    wgi_buf[i].stack_remap_slot = -1;
  }

  return 0;
}

int cp_wgi_buf_alloc() {
  for (int i = 0; i < wgi_buf_len; i++) {
    if (wgi_buf[i].in_use == 0) {
      DTRACE_SLICE_BEGIN(PF_WB_BASE + i, 0);
      wgi_buf[i].in_use = 1;
      wgi_buf[i].stack_remap_slot = -1;
      return i;
    }
  }
  return -1;
}

void cp_wgi_buf_free(int id) {
  DTRACE_SLICE_END(PF_WB_BASE + id);
  wgi_buf[id].stack_remap_slot = -1;
  wgi_buf[id].in_use = 0;
}
