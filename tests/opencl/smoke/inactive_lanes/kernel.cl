// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

__kernel void test_inactive_lanes(volatile __global int *counter,
                                  __global int *gid_out,
                                  __global int *lid_out,
                                  __global int *group_out)
{
  int slot = atomic_inc(counter);
  if (slot < 32) {
    gid_out[slot] = get_global_id(0);
    lid_out[slot] = get_local_id(0);
    group_out[slot] = get_group_id(0);
  }
}
