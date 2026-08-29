/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "kernel.h"

typedef struct {
  int *arr;
  int n;
} kargs_t;

void odd_even_step(int *arr, int n) {
  int tid = global_id();
  int num_workers = 64;

  for (int r = 0; r < n; r++) {
    int odd = r % 2;

    for (int i = tid * 2 + odd; i + 1 < n; i += num_workers * 2) {
      int a = arr[i];
      int b = arr[i + 1];

      // Branchless Swap
      int mask = (a ^ b) & -(a > b);
      arr[i] = a ^ mask;
      arr[i + 1] = b ^ mask;
    }

    __builtin_riscv_fsa_barrier(0, 0);
  }
}

void kernel(void *args) {
  kargs_t kargs = *(kargs_t *)args;
  odd_even_step(kargs.arr, kargs.n);
}
