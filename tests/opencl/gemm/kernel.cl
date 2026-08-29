// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#pragma clang fp contract (off)

// gemm kernel
// Parameters:
//  A: input matrix A, size MxK
//  B: input matrix B, size KxN
//  C: output matrix C, size MxN
__kernel void gemm (__global const TYPE *A,
                    __global const TYPE *B,
                    __global TYPE *C,
                    const int K) {

  int i = get_global_id(0);  // row
  int j = get_global_id(1);  // column
  int M = get_global_size(0);
  int N = get_global_size(1);

  if (i < M && j < N) {
    TYPE sum = 0;

    for (int k = 0; k < K; k++) {
      sum += A[i * K + k] * B[k * N + j];
    }

    C[i * N + j] = sum;
  }
}
