/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "kernel.h"

// Define the structure for arguments passed to the Kernel
typedef struct {
  int *A;
  int *B;
  int *C;
  int ni;  // Number of rows in A, Number of rows in C
  int nj;  // Number of columns in B, Number of columns in C
  int nk;  // Number of columns in A / Number of rows in B (Common dimension)
} kargs_t;

// Matrix multiplication logic: C = A * B
void matmul(int *A, int *B, int *C, int ni, int nj, int nk) {
  // Get the global thread ID
  int tid = global_id();

  // Map the 1D thread ID back to 2D matrix coordinates (Row-Major)
  // row = tid / matrix width (nj)
  // col = tid % matrix width (nj)
  int i = tid / nj;
  int j = tid % nj;

  // Boundary check: Ensure calculation range is within matrix C's dimensions
  if (i < ni && j < nj) {
    int sum = 0;
    // Perform vector dot product
    for (int k = 0; k < nk; k++) {
      sum += A[i * nk + k] * B[k * nj + j];
    }
    C[i * nj + j] = sum;
  }
}

void kernel(void *args) {
  kargs_t kargs = *(kargs_t *)args;
  matmul(kargs.A, kargs.B, kargs.C, kargs.ni, kargs.nj, kargs.nk);
}
