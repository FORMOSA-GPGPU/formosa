/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Odd lanes wait at one barrier while even lanes of the same warp wait at the
 * next one. Every lane reaches the same number of barriers, so the kernel
 * completes. */

.option norvc
.section .text
.global _start
_start:
    csrr t0, mhartid
    andi t0, t0, 1
    beqz t0, even
    fsa.bar 0, 0    /* 0x1100c: odd lanes */

even:
    fsa.bar 0, 0    /* 0x11010: even lanes, then odd lanes */
    ecall

loop:
    j loop
