/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 * SPDX-License-Identifier: Apache-2.0 */

#include <plic.h>
#include <stdint.h>

#include "platform.h"

#define MAILBOX(offset) \
  (*(volatile uint32_t *)(uintptr_t)(PLIC_TEST_MAILBOX + (offset)))

enum {
  FAIL_UNEXPECTED_TRAP = 1,
  FAIL_UNEXPECTED_CLAIM = 2,
  FAIL_ACK_TIMEOUT = 3,
  FAIL_DISABLE_SOURCE_1 = 4,
  FAIL_DISABLE_SOURCE_2 = 5,
  FAIL_THRESHOLD_READBACK = 6,
  FAIL_INITIAL_CLAIM = 7,
};

static void fail(uint32_t code) {
  MAILBOX(PLIC_TEST_ERROR) = code;
  for (;;) __asm__ volatile("" ::: "memory");
}

void plic_test_irq(void) {
  MAILBOX(PLIC_TEST_IRQ_ENTRIES) += 1;
  uintptr_t cause;
  __asm__ volatile("csrr %0, mcause" : "=r"(cause));
  if (cause != ((UINT64_C(1) << 63) | 11)) fail(FAIL_UNEXPECTED_TRAP);

  uint32_t id;
  while ((id = plic_claim(PLIC_TEST_PLIC_BASE, 0)) != 0) {
    const uint32_t seq = MAILBOX(PLIC_TEST_CLAIM_SEQ) + 1;
    if (id > 2 || seq > 3) fail(FAIL_UNEXPECTED_CLAIM);

    MAILBOX(PLIC_TEST_LOG + (seq - 1) * 4) = id;
    MAILBOX(PLIC_TEST_CLAIMED_ID) = id;
    MAILBOX(PLIC_TEST_CLAIM_SEQ) = seq;

    // Only the fixture owns this mailbox. Lua deasserts the sc.signal and
    // advances the PLIC clock before acknowledging this particular sequence.
    uint32_t timeout = 100000;
    while (MAILBOX(PLIC_TEST_ACK_SEQ) != seq) {
      if (--timeout == 0) fail(FAIL_ACK_TIMEOUT);
    }

    plic_complete(PLIC_TEST_PLIC_BASE, 0, id);
    MAILBOX(PLIC_TEST_HANDLED) = seq;
  }
}

int main(void) {
  for (uint32_t offset = 0; offset < PLIC_TEST_LOG + 12; offset += 4) {
    MAILBOX(offset) = 0;
  }

  plic_set_priority(PLIC_TEST_PLIC_BASE, 1, 1);
  plic_set_priority(PLIC_TEST_PLIC_BASE, 2, 2);
  plic_set_threshold(PLIC_TEST_PLIC_BASE, 0, 7);
  plic_enable(PLIC_TEST_PLIC_BASE, 0, 1);
  plic_enable(PLIC_TEST_PLIC_BASE, 0, 2);

  // Raw MMIO readback checks the driver's address and bitmap calculations
  // independently. Disabling either source must preserve the other enable bit.
  plic_disable(PLIC_TEST_PLIC_BASE, 0, 1);
  if (*(volatile uint32_t *)(uintptr_t)(PLIC_TEST_PLIC_BASE + 0x2000) != 4)
    fail(FAIL_DISABLE_SOURCE_1);
  plic_enable(PLIC_TEST_PLIC_BASE, 0, 1);
  plic_disable(PLIC_TEST_PLIC_BASE, 0, 2);
  if (*(volatile uint32_t *)(uintptr_t)(PLIC_TEST_PLIC_BASE + 0x2000) != 2)
    fail(FAIL_DISABLE_SOURCE_2);
  plic_enable(PLIC_TEST_PLIC_BASE, 0, 2);
  if (*(volatile uint32_t *)(uintptr_t)(PLIC_TEST_PLIC_BASE + 0x200000) != 7)
    fail(FAIL_THRESHOLD_READBACK);

  plic_set_threshold(PLIC_TEST_PLIC_BASE, 0, 0);
  if (plic_claim(PLIC_TEST_PLIC_BASE, 0) != 0) fail(FAIL_INITIAL_CLAIM);

  // Publish readiness only after both PLIC and CPU interrupt enables are set.
  __asm__ volatile("csrs mie, %0" : : "r"(UINT32_C(1) << 11) : "memory");
  __asm__ volatile("csrsi mstatus, 8" ::: "memory");
  MAILBOX(PLIC_TEST_READY) = 1;

  // Only foreground execution publishes these flags, proving mret returned
  // from the first batch of two claims and from the final repeated source.
  while (MAILBOX(PLIC_TEST_HANDLED) < 2) __asm__ volatile("" ::: "memory");
  MAILBOX(PLIC_TEST_RETURNED) = 1;

  while (MAILBOX(PLIC_TEST_HANDLED) < 3) __asm__ volatile("" ::: "memory");
  MAILBOX(PLIC_TEST_DONE) = 1;
  for (;;) __asm__ volatile("" ::: "memory");
}
