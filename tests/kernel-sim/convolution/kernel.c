/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "kernel.h"

typedef struct {
  int *input;
  int *weight;
  long long *result;
  int width;
  int height;
} kargs_t;

void conv5x5(int *input, int *weight, long long *result, int width,
             int height) {
  int tid = global_id();
  int x = tid / width;
  int y = tid % width;

  if (x >= height || y >= width) return;

  long long feature = 0;

  for (int i = x - 2; i <= x + 2; i++) {
    for (int j = y - 2; j <= y + 2; j++) {
      int x_index = i - x + 2;
      int y_index = j - y + 2;
      int m = y_index + x_index * 5;

      if (i < 0 || i >= height) continue;
      if (j < 0 || j >= width) continue;

      feature += (long long)input[i * width + j] * (long long)weight[m];
    }
  }

  result[x * width + y] = feature;
}

void kernel(void *args) {
  kargs_t kargs = *(kargs_t *)args;
  conv5x5(kargs.input, kargs.weight, kargs.result, kargs.width, kargs.height);
}
