/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "kernel.h"

typedef struct {
  uint64_t case_id;
  // A private stack slot that none of the launched threads owns.
  uint64_t stack_target;
} kargs_t;

void kernel(void *args) {
  kargs_t *kargs = (kargs_t *)args;
  uint64_t stack_target = kargs->stack_target;

  switch (kargs->case_id) {
    case 0:
      asm volatile("sd zero, 0(%0)" : : "r"(stack_target) : "memory");
      break;
    case 1:
      asm volatile("amoadd.d zero, zero, (%0)"
                   :
                   : "r"(stack_target)
                   : "memory");
      break;
    default:
      break;
  }
}
