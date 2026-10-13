/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 * SPDX-License-Identifier: Apache-2.0 */

.section .text.start
.globl _start
_start:
  .option push
  .option norelax
  la gp, __global_pointer$
  .option pop
  la sp, _stack_start
  la t0, __bss_start
  la t1, __bss_end
1:
  bgeu t0, t1, 2f
  sb zero, 0(t0)
  addi t0, t0, 1
  j 1b
2:
  li a0, 0
  li a1, 0
  call main
3:
  j 3b
