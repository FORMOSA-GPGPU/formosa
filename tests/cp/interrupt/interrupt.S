/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 * SPDX-License-Identifier: Apache-2.0 */

.section .text
.global start
start:
    li s0, 0x3000
    sw zero, 0(s0)
    la t0, trap_handler
    csrw mtvec, t0
    li t0, 0x800
    csrs mie, t0
    li t0, 8
    csrs mstatus, t0
    li t0, 1
    sw t0, 4(s0)
wait:
    lw t0, 0(s0)
    sw t0, 8(s0)
    j wait

.balign 4
# t1/t2 are reserved for the handler; foreground code uses t0.
trap_handler:
    csrr t2, mcause
    li t1, 1
    slli t1, t1, (__riscv_xlen - 1)
    ori t1, t1, 11
    bne t2, t1, unexpected
    lw t2, 0(s0)
    addi t2, t2, 1
    sw t2, 0(s0)
    # Wait for the testbench to deassert the level-triggered interrupt.
1:
    csrr t2, mip
    li t1, 0x800
    and t2, t2, t1
    bnez t2, 1b
    mret
unexpected:
    li t2, -1
    sw t2, 0(s0)
    j wait
