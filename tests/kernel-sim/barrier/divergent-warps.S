/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Warp 0 waits at one barrier while the other warps wait at another. Every
 * warp reaches one barrier, so the kernel completes. */

.option norvc
.section .text
.global _start
_start:
    csrr t0, 0xfc0  /* xwid */
    bnez t0, other
    fsa.bar 0, 0    /* 0x11008: warp 0 */
    j exit

other:
    fsa.bar 0, 0    /* 0x11010: other warps */

exit:
    ecall

loop:
    j loop
