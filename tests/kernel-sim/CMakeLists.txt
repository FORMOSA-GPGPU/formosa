# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.10)

project(kernel-sim)
enable_language(ASM)

# Using FORMOSA LLVM's clang in the nix shell for now. On macOS CMake injects
# Apple host flags (-arch, ...) that this riscv64-targeting clang rejects; route
# it through a shim that strips them (see cmake/AppleCrossCc.cmake). No-op on Linux.
apple_cross_cc_shim(CMAKE_C_COMPILER "${CLANG}")
set(CMAKE_ASM_COMPILER ${CMAKE_C_COMPILER})

set(CMAKE_C_FLAGS "--target=riscv64 -mcpu=formosa-gpgpu -O2 -mabi=lp64")
set(CMAKE_ASM_FLAGS "--target=riscv64 -mcpu=formosa-gpgpu -O2 -mabi=lp64")

# Compiles a C source file and links it with 'common/start.S'
# using specific, non-standard library linker flags.
#
# Arguments:
#   TARGET_NAME: The name of the resulting executable target.
#   SOURCE_FILE: The path to the primary C source file (e.g., 'main.c').
#
# The output elf is named ${TARGET_NAME}.kernel-sim.elf
set(KERNEL_SIM_COMMON "${CMAKE_CURRENT_SOURCE_DIR}/common")
set(KERNEL_SIM_START "${KERNEL_SIM_COMMON}/start.S")
set(LINKER_SCRIPT "${KERNEL_SIM_COMMON}/link.ld")

function(add_kernel_sim_kernel NAME SOURCE_FILE)
  set(TARGET_NAME "kernel-sim.${NAME}")

  add_executable(${TARGET_NAME}
    ${SOURCE_FILE}
    ${KERNEL_SIM_START}
  )

  target_link_options(${TARGET_NAME} PRIVATE
    -fuse-ld=lld
    -static
    -Wl,--gc-sections
    -T ${LINKER_SCRIPT}
    -nostdlib
  )

  target_include_directories(${TARGET_NAME} PRIVATE ${KERNEL_SIM_COMMON})

  set_target_properties(${TARGET_NAME} PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})
  set_target_properties(${TARGET_NAME} PROPERTIES OUTPUT_NAME "${NAME}.kernel-sim.elf")
endfunction()

set(KERNEL_SIM_SMS)
if("simtix" IN_LIST ENABLE_PROJECTS)
  list(APPEND KERNEL_SIM_SMS simtix.atomic_sm simtix.pipelined_sm)
endif()
function(add_kernel_sim_test NAME LUA_SCRIPT)
  cmake_parse_arguments(KTEST "" "" "ARGS;LABELS" ${ARGN})
  foreach(sm ${KERNEL_SIM_SMS})
    add_lv_test_script(kernel-sim.${NAME}.${sm} ${LUA_SCRIPT}
      LABELS kernel-sim ${KTEST_LABELS}
      ARGS --sm ${sm} ${KTEST_ARGS}
    )
  endforeach()
endfunction()

add_subdirectory(2mm)
add_subdirectory(bfs)
add_subdirectory(convolution)
add_subdirectory(fractal-tree)
add_subdirectory(gemm)
add_subdirectory(oesort)
add_subdirectory(relu)
add_subdirectory(riscv-test)
add_subdirectory(vecadd)
add_subdirectory(vecmul)
add_subdirectory(voronoi)
add_subdirectory(trap)
