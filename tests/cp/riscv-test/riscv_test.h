// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
// SPDX-License-Identifier: Apache-2.0

#ifndef CP_RISCV_TEST_H
#define CP_RISCV_TEST_H

#define RVTEST_RV32U
#define RVTEST_RV64U
#define TESTNUM x28

#define RVTEST_CODE_BEGIN           \
  .text;                            \
  .global TEST_FUNC_NAME;           \
  .global TEST_FUNC_RET;            \
  TEST_FUNC_NAME:                   \
  la sp, _stack_start;              \
  la a1, .test_name;                \
  call print_string;                \
  li a0, '.';                       \
  call serial_putchar;              \
  call serial_putchar;              \
  jal zero, .test_begin;            \
  .test_name :.asciz TEST_FUNC_TXT; \
  .balign 4, 0;                     \
  .test_begin:

// ISA tests may overwrite every register, including sp and ra. Reporting
// establishes its own stack and returns through the generated test label.
#define RVTEST_RESULT(COLOR, RESULT) \
  la sp, _stack_start;               \
  la a1, COLOR;                      \
  call print_string;                 \
  la a1, RESULT;                     \
  call print_string;                 \
  la a1, reset_code;                 \
  call print_string;                 \
  j TEST_FUNC_RET;

#define RVTEST_PASS RVTEST_RESULT(green_code, ok_string)
#define RVTEST_FAIL RVTEST_RESULT(red_code, err_string)
#define RVTEST_CODE_END
#define RVTEST_DATA_BEGIN .balign 8;
#define RVTEST_DATA_END

#endif
