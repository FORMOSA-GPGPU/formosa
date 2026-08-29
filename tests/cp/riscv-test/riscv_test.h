#ifndef _ENV_RV64_TEST_H
#define _ENV_RV64_TEST_H

#ifndef TEST_FUNC_NAME

#define TEST_FUNC_TXT "mytest"
#define TEST_FUNC_RET mytest_ret
#endif

#define RVTEST_RV64U
#define TESTNUM x28

#define RVTEST_CODE_BEGIN            \
  .text;                             \
  .global TEST_FUNC_NAME;            \
  .global TEST_FUNC_RET;             \
  .global serial_putchar;            \
  .global print_string;              \
  .global green_code;                \
  .global red_code;                  \
  .global reset_code;                \
  .global ok_string;                 \
  .global err_string;                \
  TEST_FUNC_NAME:                    \
  la a1, .test_name;                 \
  .prname_next : lb a0, 0(a1);       \
  beq a0, zero, .prname_done;        \
  jal serial_putchar;                \
  addi a1, a1, 1;                    \
  jal zero, .prname_next;            \
  .test_name :.ascii TEST_FUNC_TXT;  \
  .byte 0x00;                        \
  .balign 4, 0;                      \
  .prname_done : addi a0, zero, '.'; \
  jal serial_putchar;                \
  jal serial_putchar;                \
  addi sp, sp, -4;                   \
  sw ra, (sp);

#define RVTEST_PASS    \
  la sp, _stack_start; \
  sw ra, 0(sp);        \
  la a1, green_code;   \
  jal print_string;    \
  la a1, ok_string;    \
  jal print_string;    \
  la a1, reset_code;   \
  jal print_string;    \
  lw ra, 0(sp);        \
  jal zero, TEST_FUNC_RET;

#define RVTEST_FAIL    \
  la sp, _stack_start; \
  sw ra, (sp);         \
  la a1, red_code;     \
  call print_string;   \
  la a1, err_string;   \
  call print_string;   \
  la a1, reset_code;   \
  call print_string;   \
  lw ra, (sp);         \
  jal zero, TEST_FUNC_RET;

#define RVTEST_CODE_END \
  lw ra, (sp);          \
  addi sp, sp, 4;

#define RVTEST_DATA_BEGIN .balign 8;
#define RVTEST_DATA_END

#endif
