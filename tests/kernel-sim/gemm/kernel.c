/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "kernel.h"

// C_mn = alpha * (A_mk * B_kn) + beta * C_mn
typedef struct {
  int *A_arr;  // m x k
  int *B_arr;  // k x n
  int *C_arr;  // m x n
  int alpha;
  int beta;
  int m;
  int k;
  int n;
} kargs_t;

void gemm(int *A_arr, int *B_arr, int *C_arr, int alpha, int beta, int m, int k,
          int n) {
  int tid = global_id();

  // Check the legality of tid
  if (tid >= m * n) {
    return;
  }

  // Output row and col
  int o_row = tid / n;
  int o_col = tid % n;

  // Partial sum
  int sum = beta * C_arr[o_row * n + o_col];
  for (int p = 0; p < k; p++) {
    sum += alpha * (A_arr[o_row * k + p] * B_arr[p * n + o_col]);
  }

  // Store the answer
  C_arr[o_row * n + o_col] = sum;
}

void kernel(void *args) {
  kargs_t kargs = *(kargs_t *)args;
  gemm(kargs.A_arr, kargs.B_arr, kargs.C_arr, kargs.alpha, kargs.beta, kargs.m,
       kargs.k, kargs.n);
}
