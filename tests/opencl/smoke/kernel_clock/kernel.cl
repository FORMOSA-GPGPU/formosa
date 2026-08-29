// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

inline ulong hilo_to_ulong(uint2 hilo) {
    return ((ulong)hilo.y << 32) | (ulong)hilo.x;
}

inline void delay() {
    volatile ulong x = 0;
    for (int i = 0; i < 4096; i++) {
        x += (ulong)i;
    }
}

__kernel void test_clock(global ulong *out) {
    // clock_read_device()
    ulong d0 = clock_read_device();
    uint2 dh0 = clock_read_hilo_device();

    delay();

    ulong d1 = clock_read_device();
    uint2 dh1 = clock_read_hilo_device();

    out[0] = d0;
    out[1] = d1;
    out[2] = hilo_to_ulong(dh0);
    out[3] = hilo_to_ulong(dh1);

    // clock_read_work_group()
    ulong wg0 = clock_read_work_group();
    uint2 wgh0 = clock_read_hilo_work_group();

    delay();

    ulong wg1 = clock_read_work_group();
    uint2 wgh1 = clock_read_hilo_work_group();

    out[4] = wg0;
    out[5] = wg1;
    out[6] = hilo_to_ulong(wgh0);
    out[7] = hilo_to_ulong(wgh1);

    // clock_read_sub_group()
    ulong sg0 = clock_read_sub_group();
    uint2 sgh0 = clock_read_hilo_sub_group();

    delay();

    ulong sg1 = clock_read_sub_group();
    uint2 sgh1 = clock_read_hilo_sub_group();

    out[8] = sg0;
    out[9] = sg1;
    out[10] = hilo_to_ulong(sgh0);
    out[11] = hilo_to_ulong(sgh1);
}
