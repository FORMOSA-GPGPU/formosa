/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 * SPDX-License-Identifier: Apache-2.0 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int print(const char *format, ...) {
  va_list args;
  va_start(args, format);
  int count = vprintf(format, args);
  va_end(args);
  return count;
}

int main(void) {
  char name[16];
  strcpy(name, "runtime");
#if __riscv_xlen == 64
  unsigned long value = 4294967296UL;
  const char *expected = "runtime: -7 4294967296 002a X %\n";
#else
  unsigned long value = 4294967295UL;
  const char *expected = "runtime: -7 4294967295 002a X %\n";
#endif
  int count = print("%s: %d %lu %04x %c %%\n", name, -7, value, 42, 'X');
  if (strcmp(name, "runtime") || count != (int)strlen(expected)) {
    printf("runtime: FAIL\n");
    return 1;
  }
  printf("runtime: PASS\n");
  return 0;
}
