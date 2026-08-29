/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cp_reboot.h"

#include <stdint.h>

#include "cp_context.h"
#include "cp_defs.h"
#include "cp_dispatch.h"
#include "cp_memory_copy.h"
#include "cp_status.h"

/* clang-format off */
#include "FreeRTOS.h" // IWYU pragma: keep
#include "task.h"
/* clang-format on */

static volatile uint32_t reboot_requested;

void cp_reboot_request_from_isr(void) { reboot_requested = 1; }

bool cp_reboot_is_requested(void) { return reboot_requested != 0; }

void cp_reboot_perform(void) {
  taskDISABLE_INTERRUPTS();

  /* Drain already-active work only.  Queued ring entries are discarded. */
  while (cp_has_active_kernel_or_cache() || cp_memory_copy_is_active() ||
         !cp_memory_copy_dma_is_idle()) {
    taskENABLE_INTERRUPTS();
    taskYIELD();
    taskDISABLE_INTERRUPTS();
  }

  volatile struct cp_mmio *cp = (volatile struct cp_mmio *)CP_BASE;
  cp->CP_RD_PTR = 0;
  cp->CP_WR_PTR = 0;
  cp->CP_FW_HOST_ADDR = 0;
  cp->CP_FW_SIZE = 0;
  cp_firmware_status_reset();
  asm volatile("fence rw, rw" ::: "memory");

  /* Jump to boot ROM.  start.S on the next firmware load clears BSS. */
  void (*rom_entry)(void) = (void (*)(void))(uintptr_t)FSA_CP_ROM_BASE;
  rom_entry();
  for (;;) {
  }
}

/*
 * FreeRTOS routes every non-mtimer async IRQ here (see portASM.S).  Only the
 * machine software interrupt (MSIP, cause code 3) is a Firmware Reboot request
 * from the Host; ignore other sources (e.g. machine external).
 */
void freertos_risc_v_application_interrupt_handler(void) __attribute__((weak));
void freertos_risc_v_application_interrupt_handler(void) {
  uintptr_t mcause;
  asm volatile("csrr %0, mcause" : "=r"(mcause));

  /* mcause: bit XLEN-1 = interrupt, low bits = cause code. */
  const uintptr_t irq_bit = (uintptr_t)1 << (__riscv_xlen - 1);
  const uintptr_t msi = irq_bit | 3u; /* machine software interrupt */
  if (mcause != msi) {
    return;
  }

  volatile uint32_t *msip =
      (volatile uint32_t *)(uintptr_t)(FSA_CLINT_BASE + FSA_CLINT_MSIP_OFFSET);
  *msip = 0;
  cp_reboot_request_from_isr();
}
