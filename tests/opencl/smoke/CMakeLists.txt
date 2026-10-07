# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

foreach(SMOKE_CHECK IN ITEMS
  atomic_ops
  divergence
  inactive_lanes
  lmem_allocate
  spirv_smoke
  kernel_clock
  oclprintf
  copy_buffer
  memory_ops
  subgroup_info
  kernel_args
)
  add_subdirectory(${SMOKE_CHECK})
  get_property(SUITE_TESTS DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${SMOKE_CHECK}"
    PROPERTY TESTS)
  set_property(TEST ${SUITE_TESTS} DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${SMOKE_CHECK}"
    APPEND PROPERTY LABELS smoke)
endforeach()
