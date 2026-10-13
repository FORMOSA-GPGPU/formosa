/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 * SPDX-License-Identifier: Apache-2.0 */

#include <stdarg.h>
#include <stdio.h>

#include "tinyprintf.h"

extern unsigned char _serial_base;

static void serial_putchar(void *context, char c) {
  *(volatile unsigned char *)&_serial_base = (unsigned char)c;
  ++*(int *)context;
}

int vprintf(const char *format, va_list args) {
  int count = 0;
  tfp_format(&count, serial_putchar, format, args);
  return count;
}

int printf(const char *format, ...) {
  va_list args;
  int count;
  va_start(args, format);
  count = vprintf(format, args);
  va_end(args);
  return count;
}
