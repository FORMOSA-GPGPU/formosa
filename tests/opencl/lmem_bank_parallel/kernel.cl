// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

/*
 * Local-atomic hammer tuned for Formosa pipelined SM banking:
 *   - threads_per_warp = 4
 *   - warps interleaved across 4 subcores (wid % 4 == subcore_id)
 *   - LocalMem banks use 64 B lines (16 ints)
 *
 * Each warp owns one bank line. Concurrent warps 0..3 on subcores 0..3
 * therefore issue local atomics to four different banks. A single-port
 * LocalMem must serialize those masters; banked LocalMem can overlap them.
 *
 * Atomics also defeat compiler register promotion of the working set.
 */
__kernel void lmem_bank_parallel(__global int *output, __local int *tile,
                               const int iters) {
  const int lid = get_local_id(0);
  const int gid = get_global_id(0);
  const int lane = lid % 4;
  const int warp = lid / 4;
  const int idx = (warp % 4) * 16 + (warp / 4) * 4 + lane;

  tile[idx] = 0;
  barrier(CLK_LOCAL_MEM_FENCE);

  for (int i = 0; i < iters; ++i) {
    atomic_inc(&tile[idx]);
  }

  barrier(CLK_LOCAL_MEM_FENCE);
  output[gid] = tile[idx];
}
