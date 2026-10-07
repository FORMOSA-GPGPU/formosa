// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <cstdio>

#include "backprop.h"

int layer_size = 32;
void load(BPNN *);

int main(int argc, char **argv) {
  if (argc != 2) return 1;
  bpnn_initialize(7);
  BPNN *net = bpnn_create(layer_size, 16, 1);
  if (!net) return 1;
  load(net);
  float partials[32];
  for (int block = 0; block < 2; ++block) {
    for (int hidden = 1; hidden <= 16; ++hidden) {
      float sum = 0;
      for (int input = block * 16 + 1; input <= block * 16 + 16; ++input)
        sum += net->input_units[input] * net->input_weights[input][hidden];
      partials[block * 16 + hidden - 1] = sum;
    }
  }
  FILE *file = fopen(argv[1], "wb");
  if (!file) return 1;
  bool complete = fwrite(partials, sizeof(float), 32, file) == 32;
  if (fclose(file)) complete = false;
  bpnn_free(net);
  return complete ? 0 : 1;
}
