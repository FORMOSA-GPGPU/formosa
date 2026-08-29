#ifndef _KERNEL_SIM_RV64_TEST_H
#define _KERNEL_SIM_RV64_TEST_H

#ifndef TEST_FUNC_NAME
#define TEST_FUNC_NAME mytest
#define TEST_FUNC_RET mytest_ret
#endif

#define RVTEST_RV64U
#define TESTNUM x28

#define RVTEST_CODE_BEGIN \
  .text;                  \
  .global TEST_FUNC_NAME; \
  .global TEST_FUNC_RET;  \
  TEST_FUNC_NAME:

#define RVTEST_PASS jal zero, TEST_FUNC_RET;

#define RVTEST_FAIL .word 0xffffffff;

#define RVTEST_CODE_END

#define RVTEST_DATA_BEGIN .balign 8;
#define RVTEST_DATA_END

#endif
