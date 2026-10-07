# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

set(UPSTREAM "${PROJECT_SOURCE_DIR}/tests/opencl/rodinia/opencl")
configure_file("${UPSTREAM}/util/timing.c" "${CMAKE_CURRENT_BINARY_DIR}/timing_cxx.cpp" COPYONLY)
add_rodinia(bfs "${UPSTREAM}/bfs/bfs.cpp" "${CMAKE_CURRENT_BINARY_DIR}/timing_cxx.cpp")
target_link_libraries(bfs PRIVATE OpenMP::OpenMP_CXX)
target_compile_definitions(bfs PRIVATE TIMING)
foreach(SIZE 4k 32)
  # The original host uses min(node_count, 256) threads per work-group.
  set(CONFIG "")
  if(SIZE STREQUAL "4k")
    set(CONFIG large_sm)
  endif()
  add_rodinia_test(bfs.${SIZE} bfs PROGRAM bfs ID bfs SUFFIX ${SIZE}
    KIND bfs READBACK CONFIG "${CONFIG}" ARGS "${CMAKE_CURRENT_SOURCE_DIR}/graph${SIZE}.txt"
    REFERENCE_ARGS "${CMAKE_CURRENT_SOURCE_DIR}/graph${SIZE}.txt")
endforeach()
