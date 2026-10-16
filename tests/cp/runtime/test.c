/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 * SPDX-License-Identifier: Apache-2.0 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int print(const char *format, ...) {
  va_list args;
  va_start(args, format);
  int count = vprintf(format, args);
  va_end(args);
  return count;
}

extern uint64_t __udivdi3(uint64_t, uint64_t);

int main(void) {
  if (__udivdi3(UINT64_MAX, 3) != UINT64_C(0x5555555555555555) ||
      __udivdi3(UINT64_C(0x8000000000000000), 1) !=
          UINT64_C(0x8000000000000000) ||
      __udivdi3(7, UINT64_C(0x8000000000000000)) != 0 ||
      __udivdi3(7, 0) != UINT64_MAX) {
    printf("runtime: FAIL division\n");
    return 1;
  }
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
  printf("runtime: %llu\n", 18446744073709551615ULL);
  printf("runtime: PASS\n");
  return 0;
}
