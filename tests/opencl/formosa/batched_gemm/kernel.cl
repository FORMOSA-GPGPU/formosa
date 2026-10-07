// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

__kernel void batched_gemm(__global const float *A,
                           __global const float *B,
                           __global float *C,
                           const int m,
                           const int n,
                           const int k) {
  int gid0 = get_global_id(0);
  int gid1 = get_global_id(1);
  int gid2 = get_global_id(2);

  int batch = gid0;
  int row = gid1;
  int col = gid2;

  float sum = 0.0f;
  for (int inner = 0; inner < k; ++inner) {
    sum += A[batch * m * k + row * k + inner] *
           B[batch * k * n + inner * n + col];
  }
  C[batch * m * n + row * n + col] = sum;
}
