/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

.section .text
.global _start
_start:
    li x1,  0   /* ra */
    li x2,  0   /* sp */
    li x3,  0   /* gp */
    li x4,  0   /* tp */
    li x5,  0   /* t0 */
    li x6,  0   /* t1 */
    li x7,  0   /* t2 */
    li x8,  0   /* s0/fp */
    li x9,  0   /* s1 */
    li x10, 0   /* a0 */
    li x11, 0   /* a1 */
    li x12, 0   /* a2 */
    li x13, 0   /* a3 */
    li x14, 0   /* a4 */
    li x15, 0   /* a5 */
    li x16, 0   /* a6 */
    li x17, 0   /* a7 */
    li x18, 0   /* s2 */
    li x19, 0   /* s3 */
    li x20, 0   /* s4 */
    li x21, 0   /* s5 */
    li x22, 0   /* s6 */
    li x23, 0   /* s7 */
    li x24, 0   /* s8 */
    li x25, 0   /* s9 */
    li x26, 0   /* s10 */
    li x27, 0   /* s11 */
    li x28, 0   /* t3 */
    li x29, 0   /* t4 */
    li x30, 0   /* t5 */
    li x31, 0   /* t6 */

    /* stack initilization */
    la sp, _stack
    csrr a0, mhartid  /* global thread id */
    slli a1, a0, 8    /* stack for each thread is 256B */
    sub sp, sp, a1

    /* clear BSS */
    fsa.pri.raise.n 1
    bnez a0, zero_loop_end  /* thread 0 is designated to clear BSS */

    la a0, _bss_start
    la a1, _bss_end

    bge a0, a1, zero_loop_end

zero_loop:
    sd x0, 0(a0)
    addi a0, a0, 8
    ble a0, a1, zero_loop

zero_loop_end:
    fsa.pri.lower.n 1
    fsa.bar 0, 0  /* wait until all warps reach here */

    // If mhartid >= num_threads, exit directly
    csrr t0, mhartid
    csrr t1, mscratch
    ld t2, 16(t1)  // num_threads
    fsa.pri.raise.n 1
    bge t0, t2, exit

    // Load kernel argument and jump to kernel
    ld a0, 24(t1)
    call kernel

exit:
    fsa.pri.lower.n 1
    ecall

loop:
    j loop
