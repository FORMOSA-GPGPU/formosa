/* Simple CSR-based SpMV Kernel */
__kernel void spmv_csr(
    __global const int* row_ptr,
    __global const int* col_idx,
    __global const float* values,
    __global const float* x,
    __global float* y,
    const int num_rows) {

    int row = get_global_id(0);

    if (row < num_rows) {
        float sum = 0.0f;
        int start = row_ptr[row];
        int end = row_ptr[row + 1];

        /* Irregular loop: 'end - start' varies significantly in imbalanced cases */
        for (int i = start; i < end; i++) {
            sum += values[i] * x[col_idx[i]];
        }
        y[row] = sum;
    }
}
