#include <stdint.h>

const uintptr_t PRINTBUF_BASE = 0x10000000;

void putchar_(char c) {
  // use mhartid to calculate per-hart print buffer address
  uint64_t mhartid;
  asm volatile("csrr %0, mhartid" : "=r"(mhartid));
  volatile char *printbuf = (char *)(PRINTBUF_BASE + mhartid);
  *printbuf = c;
}
