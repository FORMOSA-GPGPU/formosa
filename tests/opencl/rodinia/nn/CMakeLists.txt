# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

set(UPSTREAM "${PROJECT_SOURCE_DIR}/tests/opencl/rodinia/opencl/nn")
add_rodinia(nn "${UPSTREAM}/nearestNeighbor.cpp"
  "${UPSTREAM}/clutils.cpp" "${UPSTREAM}/utils.cpp")
target_link_libraries(nn PRIVATE OpenMP::OpenMP_CXX)
target_compile_definitions(nn PRIVATE TIMING)
add_rodinia_test(nn nn PROGRAM nn ID nn KIND nn READBACK
  ARGS filelist.txt -f "${CMAKE_CURRENT_SOURCE_DIR}" -p 0 -d 0
  REFERENCE_ARGS "${CMAKE_CURRENT_SOURCE_DIR}/filelist.txt")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/filelist.txt"
  "${CMAKE_CURRENT_BINARY_DIR}/nn/filelist.txt" COPYONLY)
