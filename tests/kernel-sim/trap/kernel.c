/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "kernel.h"

typedef struct {
  uint64_t case_id;
  uint64_t fault_base;
} kargs_t;

void kernel(void *args) {
  kargs_t *kargs = (kargs_t *)args;
  uint64_t fault_base = kargs->fault_base;

  switch (kargs->case_id) {
    case 0:
      // Raw illegal instruction: expected mcause=illegal and mtval=iword.
      asm volatile(".word 0xffffffff");
      break;
    case 1:
      // beq x0, x0, +2: taken branch targets a 2-byte misaligned PC.
      asm volatile(".word 0x00000163");
      break;
    case 2:
      // Halfword load from base+1 must report the misaligned effective address.
      asm volatile("lh zero, 1(%0)" : : "r"(fault_base) : "memory");
      break;
    case 3:
      // Halfword store from base+1 must report the misaligned effective
      // address.
      asm volatile("sh zero, 1(%0)" : : "r"(fault_base) : "memory");
      break;
    default:
      break;
  }
}
