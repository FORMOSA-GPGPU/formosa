/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "kernel.h"

typedef struct {
  int *a;
  int *b;
  int *c;
  int n;
} kargs_t;

void vecadd(int *a, int *b, int *c, int n) {
  int tid = global_id();
  if (tid < n) {
    c[tid] = a[tid] + b[tid];
  }
}

void kernel(void *args) {
  kargs_t kargs = *(kargs_t *)args;
  vecadd(kargs.a, kargs.b, kargs.c, kargs.n);
}
