// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

// Work-group 0 traps at once on a misaligned store. The others work longer,
// so they retire after it.
__kernel void trap_first_group(__global uint *out, uint misalignment,
                               uint rounds) {
  const size_t gid = get_global_id(0);
  if (get_group_id(0) == 0) {
    *(__global volatile uint *)((__global uchar *)out + misalignment) = 1;
    return;
  }

  uint value = (uint)gid;
  for (uint i = 0; i < rounds; ++i) {
    value = value * 1664525u + 1013904223u;
  }
  out[gid] = value;
}
