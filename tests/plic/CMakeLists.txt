# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
# SPDX-License-Identifier: Apache-2.0

lv_register_lua_namespace(plic "${CMAKE_CURRENT_SOURCE_DIR}")

# Reuse CPU adapters and the RAM layout without enabling the CPU test suite.
lv_register_lua_namespace(cp "${formosa_SOURCE_DIR}/tests/cp")

find_program(PLIC_TEST_LUA NAMES luajit lua REQUIRED)
set(IMAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
file(MAKE_DIRECTORY "${IMAGE_DIR}")

# Lua, C, and startup assembly must agree on the test platform addresses.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${formosa_SOURCE_DIR}/tests/cp/platform.lua"
  "${CMAKE_CURRENT_SOURCE_DIR}/generate.lua"
  "${CMAKE_CURRENT_SOURCE_DIR}/platform.lua")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env
  "LUA_PATH=${LV_LUA_RUNTIME_ROOT}/?.lua;$ENV{LUA_PATH};;"
  "${PLIC_TEST_LUA}" "${CMAKE_CURRENT_SOURCE_DIR}/generate.lua" "${IMAGE_DIR}"
  COMMAND_ERROR_IS_FATAL ANY)

set(DRIVER_ROOT "${formosa_SOURCE_DIR}/lv/drivers")
set(IMAGE "${IMAGE_DIR}/rv64.elf")

# Explicit cross compilation keeps the driver and program on the same ISA/ABI,
# independent of the host compiler used for the simulation executable.
add_custom_command(OUTPUT "${IMAGE}"
  COMMAND "${CLANG}" --target=riscv64 -march=rv64im_zicsr -mabi=lp64
    -mcmodel=medany -O2 -g -ffreestanding -fno-builtin -fno-stack-protector
    -ffunction-sections -fdata-sections -Wall -Wextra -Werror
    -I "${DRIVER_ROOT}/include" -I "${IMAGE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/start.S"
    "${CMAKE_CURRENT_SOURCE_DIR}/program.c" "${DRIVER_ROOT}/src/plic.c"
    -fuse-ld=lld -nostdlib -static -Wl,--gc-sections
    -T "${IMAGE_DIR}/linker.ld" -o "${IMAGE}"
  DEPENDS start.S program.c "${DRIVER_ROOT}/src/plic.c"
    "${DRIVER_ROOT}/include/plic.h"
    "${IMAGE_DIR}/platform.h" "${IMAGE_DIR}/linker.ld"
  VERBATIM)

add_custom_target(plic-rv64.elf ALL DEPENDS "${IMAGE}")
add_lv_test_script(plic.cp.command_processor run.lua
  LABELS plic integration cp.command_processor)
set_tests_properties(plic.cp.command_processor PROPERTIES TIMEOUT 30)
