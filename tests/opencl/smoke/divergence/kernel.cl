// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

__kernel void divergence_kernel(__global int *arr, __global int *d, const int arr_size) {
    int tid = get_global_id(0);

    if (tid < arr_size) {
        if (arr[tid] > 0) {
            arr[tid] = arr[tid] + d[tid];
        } else {
            arr[tid] = 0;
        }
    }
}
