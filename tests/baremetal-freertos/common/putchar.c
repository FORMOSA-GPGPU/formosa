#include <stdint.h>

#include "console.h"

const uintptr_t PRINTBUF_BASE = 0x10000000;

static void putchar_(void *context, char c) {
  (void)context;
  // use mhartid to calculate per-hart print buffer address
  uint64_t mhartid;
  asm volatile("csrr %0, mhartid" : "=r"(mhartid));
  volatile char *printbuf = (char *)(PRINTBUF_BASE + mhartid);
  *printbuf = c;
}

void demo_console_init(void) { init_printf(0, putchar_); }
