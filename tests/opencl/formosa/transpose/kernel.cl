#pragma clang fp contract (off)

// transpose kernel
// Parameters:
// A: input matrix A, size M * N
// B: output matrix B, size N * M
__kernel void transpose(__global const TYPE *A,
                        __global TYPE *B,
                        const int M,
                        const int N) {
    int i = get_global_id(0);
    int j = get_global_id(1);

    if (i < M && j < N) {
        B[j * M + i] = A[i * N + j];
    }
}
