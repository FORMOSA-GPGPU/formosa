# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

foreach(_sm atomic pipelined)
  add_lv_test_script(simtix.sm-map.${_sm} "${CMAKE_CURRENT_SOURCE_DIR}/sm_map.lua"
    ARGS simtix.${_sm}_sm LABELS integration simtix
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
  set_tests_properties(simtix.sm-map.${_sm} PROPERTIES TIMEOUT 30)
endforeach()

foreach(_case default core four)
  add_lv_test_script(simtix.sm-config.pipelined.${_case} "${CMAKE_CURRENT_SOURCE_DIR}/sm_config.lua"
    ARGS ${_case} LABELS integration simtix
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
  set_tests_properties(simtix.sm-config.pipelined.${_case} PROPERTIES TIMEOUT 30)
endforeach()
