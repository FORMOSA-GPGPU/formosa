# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)
project(fw_host_tests LANGUAGES C)

enable_testing()

set(FW_SOURCE_PATH ${gpufw_SOURCE_DIR})

add_executable(cp_lmem_allocator_test
  cp_lmem_allocator_test.c
  ${FW_SOURCE_PATH}/cp_lmem_allocator.c
)
target_include_directories(cp_lmem_allocator_test PRIVATE
  ${FW_SOURCE_PATH}
  ${formosa_SOURCE_DIR}/hal/include
  ${formosa_SOURCE_DIR}/hal/src/hal
  ${formosa_SOURCE_DIR}/addr_map
)

if (FW_ENABLE_GCOV)
  target_compile_options(cp_lmem_allocator_test PRIVATE --coverage)
  target_link_libraries(cp_lmem_allocator_test PRIVATE --coverage)
endif()

add_test(NAME fw.cp_lmem_allocator_test
         COMMAND $<TARGET_FILE:cp_lmem_allocator_test>)
set_property(TEST fw.cp_lmem_allocator_test APPEND PROPERTY LABELS fw unit)

add_executable(cp_stack_remap_test
  cp_stack_remap_test.c
  ${FW_SOURCE_PATH}/cp_stack_remap.c
)
target_include_directories(cp_stack_remap_test PRIVATE
  ${FW_SOURCE_PATH}
  ${formosa_SOURCE_DIR}/hal/include
  ${formosa_SOURCE_DIR}/hal/src/hal
  ${formosa_SOURCE_DIR}/addr_map
)

if (FW_ENABLE_GCOV)
  target_compile_options(cp_stack_remap_test PRIVATE --coverage)
  target_link_libraries(cp_stack_remap_test PRIVATE --coverage)
endif()

add_test(NAME fw.cp_stack_remap_test
         COMMAND $<TARGET_FILE:cp_stack_remap_test>)
set_property(TEST fw.cp_stack_remap_test APPEND PROPERTY LABELS fw unit)
