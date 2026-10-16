/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 * SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>

/* RV32IM has no 64-bit divide instruction. Clang emits this compiler helper
 * for tinyprintf's unsigned long long conversions in the freestanding runtime.
 * Shifts and comparisons also compile without further runtime dependencies. */
uint64_t __udivdi3(uint64_t a, uint64_t b) {
  uint64_t quotient = 0, bit = 1;
  if (!b) return UINT64_MAX;
  while (b < a && !(b >> 63)) {
    b <<= 1;
    bit <<= 1;
  }
  while (bit) {
    if (a >= b) {
      a -= b;
      quotient |= bit;
    }
    b >>= 1;
    bit >>= 1;
  }
  return quotient;
}
