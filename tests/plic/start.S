/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 * SPDX-License-Identifier: Apache-2.0 */

#include "platform.h"

.section .text.start
.global start
start:
    # Keep gp initialization independent of its previous value.
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop
    li sp, PLIC_TEST_STACK_BASE

    # The linker aligns both BSS boundaries for full RV64 stores.
    la t0, __bss_start
    la t1, __bss_end
.Lclear_bss:
    bgeu t0, t1, .Lbss_cleared
    sd zero, 0(t0)
    addi t0, t0, 8
    j .Lclear_bss

.Lbss_cleared:
    la t0, trap_handler
    csrw mtvec, t0
    call main
.Lhalt:
    j .Lhalt

.section .text
.balign 4
trap_handler:
    # Save every RV64 caller-saved integer register. The C handler preserves
    # callee-saved registers; the 128-byte frame maintains ABI alignment.
    addi sp, sp, -128
    sd ra, 0(sp)
    sd t0, 8(sp)
    sd t1, 16(sp)
    sd t2, 24(sp)
    sd a0, 32(sp)
    sd a1, 40(sp)
    sd a2, 48(sp)
    sd a3, 56(sp)
    sd a4, 64(sp)
    sd a5, 72(sp)
    sd a6, 80(sp)
    sd a7, 88(sp)
    sd t3, 96(sp)
    sd t4, 104(sp)
    sd t5, 112(sp)
    sd t6, 120(sp)

    call plic_test_irq

    ld ra, 0(sp)
    ld t0, 8(sp)
    ld t1, 16(sp)
    ld t2, 24(sp)
    ld a0, 32(sp)
    ld a1, 40(sp)
    ld a2, 48(sp)
    ld a3, 56(sp)
    ld a4, 64(sp)
    ld a5, 72(sp)
    ld a6, 80(sp)
    ld a7, 88(sp)
    ld t3, 96(sp)
    ld t4, 104(sp)
    ld t5, 112(sp)
    ld t6, 120(sp)
    addi sp, sp, 128
    mret
