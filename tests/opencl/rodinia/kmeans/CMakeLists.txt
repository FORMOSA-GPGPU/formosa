# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

set(UPSTREAM "${PROJECT_SOURCE_DIR}/tests/opencl/rodinia/opencl")
set(SOURCES "${UPSTREAM}/kmeans/read_input.c" "${UPSTREAM}/kmeans/cluster.c"
  "${UPSTREAM}/kmeans/kmeans_clustering.c" "${UPSTREAM}/kmeans/rmse.c")
# Match the original g++ build, using the system POSIX getopt implementation.
set_source_files_properties(${SOURCES} PROPERTIES LANGUAGE CXX)
configure_file("${UPSTREAM}/util/timing.c" "${CMAKE_CURRENT_BINARY_DIR}/timing_cxx.cpp" COPYONLY)
add_rodinia(kmeans "${UPSTREAM}/kmeans/kmeans.cpp" ${SOURCES}
  "${CMAKE_CURRENT_BINARY_DIR}/timing_cxx.cpp")
target_link_libraries(kmeans PRIVATE OpenMP::OpenMP_CXX)
target_compile_definitions(kmeans PRIVATE TIMING)
# Original allocate() lacks a return, and its callers ignore the value.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(kmeans PRIVATE -O0 -fno-unreachable-traps)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(kmeans PRIVATE -fno-strict-return)
endif()
set(INPUT "${INPUT_DIR}/kmeans100.txt")
add_rodinia_test(kmeans kmeans PROGRAM kmeans ID kmeans KIND kmeans READBACK CONFIG large_sm
  ARGS -i "${INPUT}" -n 5 -m 5 -o REFERENCE_ARGS "${INPUT}")
