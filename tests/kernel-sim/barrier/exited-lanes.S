/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Lane 3 of each warp exits, like a lane past the work-group size, while
 * lanes 0-2 meet at the barrier. The lanes that exited count as arrived, so
 * the kernel completes. */

.option norvc
.section .text
.global _start
_start:
    csrr t0, mhartid
    andi t0, t0, 3
    li t1, 3
    beq t0, t1, exit
    fsa.bar 0, 0
    nop

exit:
    ecall

loop:
    j loop
