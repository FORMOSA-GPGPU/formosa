// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

__kernel void inclusive_prefix_sum(__global const int *input,
                                   __global int *output,
                                   __local int *scratch_a,
                                   __local int *scratch_b,
                                   const uint count) {
  const size_t lid = get_local_id(0);
  const size_t local_size = get_local_size(0);

  for (size_t i = lid; i < count; i += local_size) {
    scratch_a[i] = input[i];
  }
  barrier(CLK_LOCAL_MEM_FENCE);

  // Ping-pong local buffers keep every stride's reads separate from its
  // writes. A work-item may handle multiple entries when count exceeds the
  // device's maximum work-group size.
  __local int *source = scratch_a;
  __local int *destination = scratch_b;
  for (size_t offset = 1; offset < count; offset <<= 1) {
    for (size_t i = lid; i < count; i += local_size) {
      destination[i] = source[i];
      if (i >= offset) {
        destination[i] += source[i - offset];
      }
    }
    barrier(CLK_LOCAL_MEM_FENCE);
    __local int *swap = source;
    source = destination;
    destination = swap;
  }

  for (size_t i = lid; i < count; i += local_size) {
    output[i] = source[i];
  }
}
