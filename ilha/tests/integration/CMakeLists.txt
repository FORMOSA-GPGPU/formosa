# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

add_lv_test_script(ilha.wg_lifecycle "${CMAKE_CURRENT_SOURCE_DIR}/wg_lifecycle.lua"
  LABELS integration ilha WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../..")

# A standalone idle ROM exercises the real system without runtime firmware or
# software platform data from another subproject.
find_program(ILHA_TEST_RV64_CC NAMES riscv64-none-elf-gcc REQUIRED)
set(_idle_rom "${CMAKE_CURRENT_BINARY_DIR}/idle-rom.elf")
add_custom_command(
  OUTPUT "${_idle_rom}"
  COMMAND "${ILHA_TEST_RV64_CC}" -march=rv64im_zicsr -mabi=lp64 -mcmodel=medany
          -nostdlib -Wl,--build-id=none -Wl,-Ttext=0 -Wl,-e,start
          "${CMAKE_CURRENT_SOURCE_DIR}/idle_rom.S" -o "${_idle_rom}"
  DEPENDS idle_rom.S
  VERBATIM)
add_custom_target(ilha-system-fixtures ALL DEPENDS "${_idle_rom}")

foreach(_sm atomic pipelined)
  add_lv_test_script(ilha.system.${_sm} "${CMAKE_CURRENT_SOURCE_DIR}/system_sm.lua"
    ARGS "${_idle_rom}" simtix.${_sm}_sm LABELS integration ilha
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
  set_tests_properties(ilha.system.${_sm} PROPERTIES TIMEOUT 30)
endforeach()
