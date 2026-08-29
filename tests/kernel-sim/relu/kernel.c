/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "kernel.h"

typedef struct {
  float *arr;
  int n;
} kargs_t;

void relu(float *arr, int n) {
  int tid = global_id();
  if (tid < n && arr[tid] < 0) {
    arr[tid] = 0;
  }
}

void kernel(void *args) {
  kargs_t kargs = *(kargs_t *)args;
  relu(kargs.arr, kargs.n);
}
