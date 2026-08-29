// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

__kernel void oclprintf (__global const int *A)
{
  int gid = get_global_id(0);
  printf("A[%d] = %d\n", gid, A[gid]);
}
