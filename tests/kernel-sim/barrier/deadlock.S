/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Warp 0 exits while the other warps wait at a barrier it never reaches. */

.option norvc
.section .text
.global _start
_start:
    csrr t0, 0xfc0  /* xwid */
    beqz t0, exit
    fsa.bar 0, 0    /* 0x11008 */

exit:
    ecall

loop:
    j loop
