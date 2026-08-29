# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

macro(add_simtix_unit_test NAME SCRIPT)
  add_lv_test_script(simtix.${NAME} ${SCRIPT} ${ARGN} LABELS "simtix" "unit")
endmacro()

add_subdirectory(cache)
add_subdirectory(utils)
add_subdirectory(pipelined_core)
# add_subdirectory(unit)
