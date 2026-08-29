# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.10)

project(pr_tester LANGUAGES C)
enable_language(ASM)

set(TARGET pfreader_tester.elf)

# Using FORMOSA LLVM's clang in the nix shell for now.
set(CMAKE_C_COMPILER ${CLANG})
set(CMAKE_ASM_COMPILER ${CMAKE_C_COMPILER})
set(CMAKE_C_FLAGS "--target=riscv64 -march=rv64im")
set(CMAKE_ASM_FLAGS "--target=riscv64 -march=rv64im")
set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld)

add_executable(${TARGET}
  main.c
  pfreader_util.c
  start.S
)

set_target_properties(${TARGET} PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})
target_link_options(${TARGET} PRIVATE
  -fuse-ld=lld -static -Wl,--gc-sections -T ${LINKER_SCRIPT} -static -nostdlib)

add_lv_test_script(pfreader pfreader.lua ARGS $<TARGET_FILE:${TARGET}> LABELS integration)
