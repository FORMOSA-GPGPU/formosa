# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
# SPDX-License-Identifier: Apache-2.0

include(add_riscv_test_elf.cmake)
foreach(CORE IN LISTS CP_SELECTED_CORES)
  add_cp_riscv_image("${CORE}")
  add_lv_test_script(cp.riscv-test.${CORE} run.lua
    ARGS --core "${CORE}"
    LABELS cp riscv-test integration ${CORE})
  set_tests_properties(cp.riscv-test.${CORE} PROPERTIES TIMEOUT 120)
endforeach()
