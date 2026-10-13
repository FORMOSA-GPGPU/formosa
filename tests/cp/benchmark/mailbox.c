/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 * SPDX-License-Identifier: Apache-2.0 */

#include "benchmark.h"

/* Mailbox shared with the cp.benchmark.check measurement checker. */
static volatile unsigned result[3] __attribute__((section(".benchmark")));

void benchmark_start(void) {
  __asm__ volatile("" ::: "memory");
  result[0] = 1;
  __asm__ volatile("" ::: "memory");
}

void benchmark_stop(void) {
  __asm__ volatile("" ::: "memory");
  result[0] = 2;
  __asm__ volatile("" ::: "memory");
}

void benchmark_finish(unsigned iterations, unsigned errors) {
  result[1] = iterations;
  result[2] = errors;
  result[0] = 3;
  for (;;) __asm__ volatile("j .");
}
