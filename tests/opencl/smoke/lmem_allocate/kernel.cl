// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

__kernel void lmem_allocate(
    __global uint *output
) {
    __local uint lmem[LMEMSIZE / sizeof(uint)]; // local memory allocation

    uint group_id = (uint)get_group_id(0);
    uint local_id = (uint)get_local_id(0);
    uint local_size = (uint)get_local_size(0);

    lmem[local_id] = group_id * local_size + local_id;
    barrier(CLK_LOCAL_MEM_FENCE);

    if (local_id == 0) {
        uint sum = 0;
        for (uint i = 0; i < local_size; ++i) {
            sum += lmem[i];
        }
        output[group_id] = sum;
    }
}
