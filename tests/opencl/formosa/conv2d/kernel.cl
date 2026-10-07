// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#pragma clang fp contract (off)

// conv2d kernel
__kernel void conv2d (__global const TYPE* input,
                      __global const TYPE* conv_kernel,
                      __global TYPE* output,
                      const int input_size,
                      const int kernel_size,
                      const int stride) {
    int x = get_global_id(0);
    int y = get_global_id(1);
    int output_size = get_global_size(0);

    int input_x = x * stride;
    int input_y = y * stride;

    TYPE sum = 0;
    for (int i = 0; i < kernel_size; i++) {
        for (int j = 0; j < kernel_size; j++) {
            sum += input[(input_x + i) * input_size + (input_y + j)] * conv_kernel[i * kernel_size + j];
        }
    }
    output[x * output_size + y] = sum;
}
