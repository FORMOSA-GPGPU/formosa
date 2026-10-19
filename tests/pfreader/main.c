/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <lv/drivers/simple/pfreader.h>

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
  const uintptr_t base = (uintptr_t)&_pfreader_base;
  pfreader_slice_begin(base, 0, 0);
  for (int i = 0; i < 10; i++) {
    if (i == 3) {
      pfreader_slice_end(base, 0);
      pfreader_slice_begin(base, 0, 1);
    }
    pfreader_instant(base, 1, i % 2);
    pfreader_counter(base, 2, i);
  }
  pfreader_slice_end(base, 0);
  print_string("[CP] End\n");
  return 0;
}
