# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

add_subdirectory(riscv-test)
add_lv_test_script(cp cp.lua ARGS $<TARGET_FILE:riscv-test.elf> LABELS "integration")
set_property(TEST cp APPEND PROPERTY FAIL_REGULAR_EXPRESSION "ERR")
add_lv_test_script(scalar_core_riscv_test scalar_core.lua ARGS $<TARGET_FILE:riscv-test.elf> LABELS "integration")
set_property(TEST scalar_core_riscv_test APPEND PROPERTY FAIL_REGULAR_EXPRESSION "ERR")
