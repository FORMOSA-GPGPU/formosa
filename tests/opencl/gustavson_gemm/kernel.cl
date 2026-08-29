// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

/**
 * Gustavson GEMM with Host-allocated Local Memory
 * Each Work-group handles one Row 'i'.
 * Threads within the Work-group cooperate to update local_row.
 */
__kernel void gustavson_gemm (__global const TYPE *A,
                    __global const TYPE *B,
                    __global TYPE *C,
                    const int K,
                    const int N,
                    __local TYPE *local_row) { // Dynamic local memory from Host

    // group_id(0) is the Row index 'i'
    int i = get_group_id(0);
    // local_id(0) is the thread index within the group
    int tid = get_local_id(0);
    int local_size = get_local_size(0);

    // 1. Initialize local_row (Collective effort)
    // Even if N > local_size, this loop handles it.
    for (int j = tid; j < N; j += local_size) {
        local_row[j] = 0;
    }

    // Barrier: Ensure all threads finished initialization before computation
    barrier(CLK_LOCAL_MEM_FENCE);

    // 2. Gustavson Core Logic with Zero-skipping
    for (int k = 0; k < K; k++) {
        // Read A[i, k] - This is naturally a "Broadcast" within the Work-group
        TYPE valA = A[i * K + k];

        if (valA != (TYPE)0) {
            // All threads in WG work on different columns 'j' of the same row
            for (int j = tid; j < N; j += local_size) {
                local_row[j] += valA * B[k * N + j];
            }
        }
        // Note: No barrier needed inside k-loop because each 'tid' works on a unique 'j'
    }

    // 3. Final Synchronization: Wait for all k-iterations to finish
    barrier(CLK_LOCAL_MEM_FENCE);

    // 4. Coalesced Write-back to Global Memory
    for (int j = tid; j < N; j += local_size) {
        C[i * N + j] = local_row[j];
    }
}
