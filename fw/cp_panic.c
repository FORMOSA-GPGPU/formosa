/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cp_panic.h"

/* clang-format off */
#include "FreeRTOS.h"
#include "task.h"
/* clang-format on */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

_Noreturn void cp_panic(const char *fmt, ...) {
  taskDISABLE_INTERRUPTS();

  va_list args;
  va_start(args, fmt);
  fprintf(stderr, "\033[31mCP PANIC: ");
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\033[0m\r\n");
  va_end(args);

  abort();  // Stop firmware
}
