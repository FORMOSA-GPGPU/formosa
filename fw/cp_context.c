/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cp_context.h"

#include "cp_defs.h"
#include "cp_trace.h"

struct cp_kernel_state kernel_state_buf[CP_KERNEL_STATE_BUF_SIZE] = {0};
const int kernel_state_buf_size = CP_KERNEL_STATE_BUF_SIZE;

struct cp_cache_state cache_state = {0};
struct cp_memory_copy_state memory_copy_state = {0};

int cp_kernel_state_buf_alloc() {
  for (int i = 0; i < kernel_state_buf_size; i++) {
    if (!kernel_state_buf[i].in_use) {
      DTRACE_SLICE_BEGIN(PF_KB_BASE + i, 0);
      return i;
    }
  }
  return -1;
}

void cp_kernel_state_buf_free(int id) {
  DTRACE_SLICE_END(PF_KB_BASE + id);
  kernel_state_buf[id].in_use = 0;
}
