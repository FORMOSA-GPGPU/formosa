/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "formosa.h"

#include <FreeRTOS.h>
#include <libc.h>
#include <stdint.h>
#include <stdlib.h>

/* Use FreeRTOS provided malloc/free */
void *formosa_malloc(size_t n) { return pvPortMalloc(n); }

void formosa_free(void *ptr) { vPortFree(ptr); }

void formosa_putc(char c) __attribute__((weak));
void formosa_putc(char c) { *(volatile char *)FORMOSA_SERIAL_BASE = c; }

void formosa_puts(const char *s, size_t n) {
  for (int i = 0; i < n; i++) {
    formosa_putc(s[i]);
  }
}

/* nnlc requires runtime to accept ptr == NULL or size <= 0. In my case, the for
 * loop in `formosa_puts()` will pass the size <= 0 case */
ssize_t formosa_write_stdout(const void *d, size_t sz) {
  if (d == NULL) return 0;
  formosa_puts(d, sz);
  return sz;
}

ssize_t formosa_write_stderr(const void *d, size_t sz) {
  if (d == NULL) return 0;
  formosa_puts(d, sz);
  return sz;
}

extern void _deadloop();
__attribute__((noreturn)) void formosa_exit(int n) {
  *(volatile int *)FORMOSA_EXIT_BASE = n;
  _deadloop();
  __builtin_unreachable();
}

uint64_t formosa_usleep(uint64_t micro_seconds) { return 0; }

/* The two gettime functions do nothing, I will not use these in firmware for
 * now */
int formosa_gettime_monotonic(uint64_t *secs, uint64_t *nanosecs) {
  *secs = 0;
  *nanosecs = 0;
  return 0;
}

int formosa_gettime_wall(uint64_t *secs, uint64_t *nanosecs) {
  *secs = 0;
  *nanosecs = 0;
  return 0;
}

static const struct nnlc_sysdeps formosa_sysdeps = {
    .malloc = formosa_malloc,
    .free = formosa_free,
    .write_stdout = formosa_write_stdout,
    .write_stderr = formosa_write_stderr,
    .exit = formosa_exit,
    .usleep = formosa_usleep,
    .gettime_monotonic = formosa_gettime_monotonic,
    .gettime_wall = formosa_gettime_wall,
};

void formosa_libc_init() { _nnlc_initialize(&formosa_sysdeps); }
