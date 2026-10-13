# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
# SPDX-License-Identifier: Apache-2.0

function(add_cp_benchmark_image IMAGE XLEN)
  set(SUPPORT "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
  add_cp_runtime(rv${XLEN}im)
  add_executable(${IMAGE} "${SUPPORT}/mailbox.c")
  target_link_libraries(${IMAGE} PRIVATE cp-runtime-rv${XLEN}im)
  set_target_properties(${IMAGE} PROPERTIES
    OUTPUT_NAME "rv${XLEN}im" SUFFIX ".elf"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    LINK_DEPENDS "${SUPPORT}/linker.ld")
  target_include_directories(${IMAGE} PRIVATE "${SUPPORT}")
  # Keep benchmark optimization independent of the simulator build profile.
  target_compile_options(${IMAGE} PRIVATE -O2 -fno-lto)
  target_link_options(${IMAGE} PRIVATE -fno-lto -T "${SUPPORT}/linker.ld")
endfunction()
