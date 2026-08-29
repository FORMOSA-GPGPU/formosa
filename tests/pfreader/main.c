/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pfreader_util.h"

extern uint64_t _pfreader_base;
void serial_putchar(char c);

void print_string(char *s) {
  char c;
  while ((c = *s++)) {
    serial_putchar(c);
  }
}

int main() {
  print_string("[CP] Start\n");
  pr_init((int64_t *)&_pfreader_base);
  pr_slice_begin(0, 0);
  for (int i = 0; i < 10; i++) {
    if (i == 3) {
      pr_slice_end(0);
      pr_slice_begin(0, 1);
    }
    pr_instant(1, i % 2);
    pr_counter(2, i);
  }
  pr_slice_end(0);
  print_string("[CP] End\n");
  return 0;
}
