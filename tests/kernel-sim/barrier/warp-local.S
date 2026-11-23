/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Warp 0 exchanges local/global memory while the other warps wait at a
 * work-group barrier. Lane 0 arrives first, before its peers write data.
 * Repeat with all lanes, then with the last lane exiting. */
.option norvc
.section .text
.global _start
_start:
    csrr s0, 0xfd0  /* xlaneid */
    csrr s1, 0xfe0  /* xlanes */
    csrr t0, mhartid
    divu t0, t0, s1
    fsa.bar 0, 0
    bnez t0, work_group_join

    slli s2, s0, 3
    li s3, 0x23000
    add s3, s3, s2
    addi s4, s1, -1
    li s5, 1
    li s6, 4

round:
    beqz s0, write_data
    li t0, 16
delay:
    addi t0, t0, -1
    bnez t0, delay
write_data:
    sd s5, 0(s2)
    sd s5, 0(s3)
    fsa.bar 0, 1

    slli t0, s4, 3
    ld t1, 0(t0)
    bne t1, s5, fail
    li t2, 0x23000
    add t2, t2, t0
    ld t1, 0(t2)
    bne t1, s5, fail
    fsa.bar 0, 1
    addi s5, s5, 1
    blt s5, s6, round

    li t0, 4
    bne s5, t0, work_group_join
    beq s0, s4, exit
    addi s4, s4, -1
    li s6, 7
    j round

work_group_join:
    fsa.bar 0, 0
    fsa.bar 0, 1
exit:
    ecall
    j exit

fail:
    unimp
