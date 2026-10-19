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

# Keep the guest compiler local to this directory, as in the other CPU tests.
enable_language(ASM)
apple_cross_cc_shim(CMAKE_C_COMPILER "${CLANG}")
set(CMAKE_ASM_COMPILER "${CMAKE_C_COMPILER}")
# Host instrumentation and linker flags do not apply to this freestanding guest.
set(CMAKE_C_FLAGS "")
set(CMAKE_ASM_FLAGS "")
set(CMAKE_EXE_LINKER_FLAGS "")

set(RISCV_ARCH_FLAGS --target=riscv64 -march=rv64im_zicsr -mabi=lp64
  -mcmodel=medany)
add_executable(plic-rv64.elf start.S program.c)
target_link_libraries(plic-rv64.elf PRIVATE lv::device)
target_include_directories(plic-rv64.elf PRIVATE "${IMAGE_DIR}")
target_compile_options(plic-rv64.elf PRIVATE
  ${RISCV_ARCH_FLAGS} -O2 -g -ffreestanding -fno-builtin -fno-stack-protector
  -ffunction-sections -fdata-sections -Wall -Wextra -Werror)
target_link_options(plic-rv64.elf PRIVATE
  ${RISCV_ARCH_FLAGS} -fuse-ld=lld -nostdlib -static -Wl,--gc-sections
  -T "${IMAGE_DIR}/linker.ld")
set_target_properties(plic-rv64.elf PROPERTIES
  OUTPUT_NAME rv64 SUFFIX ".elf"
  RUNTIME_OUTPUT_DIRECTORY "${IMAGE_DIR}"
  LINK_DEPENDS "${IMAGE_DIR}/linker.ld")

add_lv_test_script(plic.cp.command_processor run.lua
  LABELS plic integration cp.command_processor)
set_tests_properties(plic.cp.command_processor PROPERTIES TIMEOUT 30)
