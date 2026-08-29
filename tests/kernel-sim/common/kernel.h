/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef KERNEL_H_
#define KERNEL_H_

#include <stdint.h>

typedef struct {
  uint64_t group_id;
  uint64_t group_size;
  uint64_t num_threads;
  uint64_t kargs;
} info_t;

static inline int local_id() {
  int result;
  asm volatile("csrr %0, %1" : "=r"(result) : "i"(0xf14));
  return result;
}

static inline int global_id() {
  info_t *info;
  asm volatile("csrr %0, %1" : "=r"(info) : "i"(0x340));
  return info->group_id * info->group_size + local_id();
}

#endif
