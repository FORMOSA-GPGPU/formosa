# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

set(UPSTREAM "${PROJECT_SOURCE_DIR}/tests/opencl/rodinia/opencl/gaussian")
add_rodinia(gaussian "${UPSTREAM}/gaussianElim.cpp"
  "${UPSTREAM}/clutils.cpp" "${UPSTREAM}/utils.cpp")
target_link_libraries(gaussian PRIVATE OpenMP::OpenMP_CXX)
target_compile_definitions(gaussian PRIVATE TIMING)
foreach(SIZE 4 25 33)
  add_rodinia_test(gaussian.${SIZE} gaussian PROGRAM gaussian ID gaussian SUFFIX ${SIZE}
    KIND gaussian READBACK
    ARGS -f "${CMAKE_CURRENT_SOURCE_DIR}/matrix${SIZE}.txt" -p 0 -d 0
    REFERENCE_ARGS "${CMAKE_CURRENT_SOURCE_DIR}/matrix${SIZE}.txt")
endforeach()
