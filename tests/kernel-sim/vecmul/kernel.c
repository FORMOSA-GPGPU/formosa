/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "kernel.h"

typedef struct {
  int *a;
  int *b;
  long long *c;
  int n;
} kargs_t;

void vecmul(int *a, int *b, long long *c, int n) {
  int tid = global_id();
  if (tid < n) {
    c[tid] = (long long)a[tid] * (long long)b[tid];
  }
}

void kernel(void *args) {
  kargs_t kargs = *(kargs_t *)args;
  vecmul(kargs.a, kargs.b, kargs.c, kargs.n);
}
