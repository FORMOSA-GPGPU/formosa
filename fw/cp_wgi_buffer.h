/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CP_WGI_BUFFER_H
#define CP_WGI_BUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint64_t printf_buffer;
  uint64_t printf_buffer_position;
  uint32_t printf_buffer_capacity;
  uint32_t reserved;
} PoclPrintfLaunchMeta;

typedef struct WGInfo {
  uint32_t dim;
  uint32_t wg_id[3];
  uint32_t local_size[3];
  uint32_t num_groups[3];
  uint32_t global_offset[3];
  uint32_t num_threads;
  uint64_t trampoline;
  uint64_t kargs;
  uint64_t stack_base;  // stack base address for this work-group
  uint32_t stack_size;  // stack size for a work-item
  uint64_t local_memory_base;
  uint32_t local_memory_size;
  uint64_t printf_buffer;
  uint64_t printf_buffer_position;
  uint32_t printf_buffer_capacity;
  uint32_t reserved0;
} WGInfo;

_Static_assert(offsetof(WGInfo, stack_base) == 0x48,
               "WGInfo.stack_base offset mismatch");
_Static_assert(offsetof(WGInfo, printf_buffer) == 0x68,
               "WGInfo.printf_buffer offset mismatch");
struct cp_wgi_buf {
  WGInfo wginfo;
  volatile int in_use;
  int wbid;
  int kid;
  int smid;
  int stack_remap_slot;
  uint64_t kernel_status_ptr;
};

/* Lockless slot pool for one dispatch producer and one retire consumer. */
extern size_t wgi_buf_len;
extern struct cp_wgi_buf *const wgi_buf;

int cp_wgi_buf_init();
int cp_wgi_buf_alloc();
void cp_wgi_buf_free(int id);

#endif
