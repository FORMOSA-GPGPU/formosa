# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

find_package(Python3 REQUIRED COMPONENTS Interpreter)
include(CheckSymbolExists)
include(CheckIncludeFileCXX)

set(UPSTREAM "${PROJECT_SOURCE_DIR}/tests/opencl/shoc")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_LIST_DIR}/generate_inputs.py")

# These are the platform checks used by the original serial common library.
check_symbol_exists(clock_gettime "time.h" HAVE_CLOCK_GETTIME)
check_symbol_exists(CLOCK_PROCESS_CPUTIME_ID "time.h" HAVE_CLOCK_PROCESS_CPUTIME_ID)
check_symbol_exists(gettimeofday "sys/time.h" HAVE_GETTIMEOFDAY)
check_include_file_cxx(stdint.h HAVE_STDINT_H)
configure_file(config.h.in "${CMAKE_CURRENT_BINARY_DIR}/config.h")

set(COMMON "${UPSTREAM}/src/common")
set(OPENCL_COMMON "${UPSTREAM}/src/opencl/common")
add_library(shoc_common STATIC
  "${COMMON}/CTimer.cpp" "${COMMON}/ResultDatabase.cpp"
  "${COMMON}/OptionParser.cpp" "${COMMON}/Option.cpp" "${COMMON}/Timer.cpp"
  "${COMMON}/ProgressBar.cpp" "${COMMON}/InvalidArgValue.cpp"
  "${COMMON}/Matrix2DStatics.cpp")
target_include_directories(shoc_common PUBLIC "${COMMON}" "${CMAKE_CURRENT_BINARY_DIR}")
set_target_properties(shoc_common PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES)
add_library(shoc_opencl_common STATIC
  "${OPENCL_COMMON}/OpenCLDeviceInfo.cpp" "${OPENCL_COMMON}/OpenCLPlatform.cpp"
  "${OPENCL_COMMON}/OpenCLNodePlatformContainer.cpp" "${OPENCL_COMMON}/Event.cpp")
target_include_directories(shoc_opencl_common PUBLIC "${OPENCL_COMMON}")
target_compile_definitions(shoc_opencl_common PUBLIC ${OCL_DEFAULT_OPTS})
target_link_libraries(shoc_opencl_common PUBLIC shoc_common OpenCL::OpenCL)
set_target_properties(shoc_opencl_common PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES)

set(INPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/inputs")
execute_process(COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_LIST_DIR}/generate_inputs.py" "${INPUT_DIR}"
  COMMAND_ERROR_IS_FATAL ANY)

function(add_shoc NAME DIRECTORY KERNEL)
  set(SOURCE_DIR "${UPSTREAM}/src/opencl/${DIRECTORY}")
  set(EMBEDDED_SOURCES)
  foreach(KERNEL_NAME IN LISTS KERNEL)
    set(EMBEDDED "${CMAKE_CURRENT_BINARY_DIR}/${KERNEL_NAME}_cl.cpp")
    # Match config/targets.mk.in: strip CR, escape C literals, retain each LF.
    add_custom_command(OUTPUT "${EMBEDDED}"
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/embed_kernel.py"
        "${SOURCE_DIR}/${KERNEL_NAME}.cl" "${EMBEDDED}" "cl_source_${KERNEL_NAME}"
      DEPENDS "${SOURCE_DIR}/${KERNEL_NAME}.cl" "${CMAKE_CURRENT_LIST_DIR}/embed_kernel.py"
      VERBATIM)
    list(APPEND EMBEDDED_SOURCES "${EMBEDDED}")
  endforeach()
  add_executable(shoc_${NAME} ${ARGN} ${EMBEDDED_SOURCES} "${OPENCL_COMMON}/main.cpp")
  target_include_directories(shoc_${NAME} PRIVATE "${SOURCE_DIR}")
  target_link_libraries(shoc_${NAME} PRIVATE shoc_opencl_common)
  set_target_properties(shoc_${NAME} PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES)
endfunction()

add_shoc(spmv level1/spmv spmv "${UPSTREAM}/src/opencl/level1/spmv/Spmv.cpp")
set(STENCIL_DIR "${UPSTREAM}/src/opencl/level1/stencil2d")
add_shoc(stencil2d level1/stencil2d stencil2d
  "${STENCIL_DIR}/Stencil2Dmain.cpp" "${STENCIL_DIR}/OpenCLStencil.cpp"
  "${STENCIL_DIR}/OpenCLStencilFactory.cpp" "${STENCIL_DIR}/CommonOpenCLStencilFactory.cpp")
foreach(CASE IN ITEMS BusSpeedDownload BusSpeedReadback DeviceMemory KernelCompile MaxFlops QueueDelay)
  string(TOLOWER "${CASE}" NAME)
  add_shoc(${NAME} level0 "" "${UPSTREAM}/src/opencl/level0/${CASE}.cpp")
endforeach()
target_compile_definitions(shoc_queuedelay PRIVATE SHOC_DEFAULT_PASSES=\"3\")
add_shoc(bfs level1/bfs "bfs_iiit;bfs_uiuc_spill"
  "${UPSTREAM}/src/opencl/level1/bfs/BFS.cpp" "${COMMON}/Graph.cpp")
add_shoc(fft level1/fft fft "${UPSTREAM}/src/opencl/level1/fft/FFT.cpp"
  "${UPSTREAM}/src/opencl/level1/fft/fftlib.cpp")
add_shoc(gemm level1/gemm gemmN "${UPSTREAM}/src/opencl/level1/gemm/GEMM.cpp")
add_shoc(md level1/md md "${UPSTREAM}/src/opencl/level1/md/MD.cpp")
add_shoc(md5hash level1/md5hash md5 "${UPSTREAM}/src/opencl/level1/md5hash/MD5Hash.cpp")
add_shoc(reduction level1/reduction reduction "${UPSTREAM}/src/opencl/level1/reduction/Reduction.cpp")
add_shoc(scan level1/scan scan "${UPSTREAM}/src/opencl/level1/scan/Scan.cpp")
add_shoc(sort level1/sort sort "${UPSTREAM}/src/opencl/level1/sort/Sort.cpp")
add_shoc(triad level1/triad "" "${UPSTREAM}/src/opencl/level1/triad/Triad.cpp")
add_shoc(s3d level2/s3d
  "gr_base;qssa;qssab;qssa2;ratt;ratt2;ratt3;ratt4;ratt5;ratt6;ratt7;ratt8;ratt9;ratt10;ratx;ratxb;ratx2;ratx4;rdsmh;rdwdot;rdwdot2;rdwdot3;rdwdot6;rdwdot7;rdwdot8;rdwdot9;rdwdot10"
  "${UPSTREAM}/src/opencl/level2/s3d/S3D.cpp")
add_test(NAME opencl.shoc.validation
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/verify_test.py"
    "${CMAKE_CXX_COMPILER}" "${UPSTREAM}/src" "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(opencl.shoc.validation PROPERTIES LABELS "integration;opencl;shoc" TIMEOUT 30)

function(add_shoc_test NAME)
  cmake_parse_arguments(CASE "" "CONFIG;TARGET;KIND" "" ${ARGN})
  if(NOT CASE_TARGET)
    set(CASE_TARGET shoc_${NAME})
  endif()
  if(NOT CASE_KIND)
    set(CASE_KIND "${NAME}")
  endif()
  set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/${NAME}")
  file(MAKE_DIRECTORY "${WORK_DIR}")
  add_lv_opencl_integration_test(${CASE_TARGET}
    ID shoc.${NAME}
    CONFIG "${CASE_CONFIG}"
    WORKING_DIRECTORY "${WORK_DIR}"
    WRAPPER "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/verify.py" --kind "${CASE_KIND}" --
    ARGS ${CASE_UNPARSED_ARGUMENTS})
  foreach(SM IN LISTS OPENCL_SMS)
    set_property(TEST opencl.shoc.${NAME}.${SM} APPEND PROPERTY LABELS benchmark shoc)
    # Leave headroom for Debug/coverage and concurrent CI workloads.
    set_tests_properties(opencl.shoc.${NAME}.${SM} PROPERTIES
      RESOURCE_LOCK shoc_${NAME} TIMEOUT 600)
  endforeach()
endfunction()

add_shoc_test(spmv CONFIG large_sm --passes 1 --iterations 1 --mm_filename "${INPUT_DIR}/matrix.mtx")
# All ten comparisons exceed the suite's 180-second limit in Debug CI on atomic SM.
foreach(SM IN LISTS OPENCL_SMS)
  set_tests_properties(opencl.shoc.spmv.${SM} PROPERTIES TIMEOUT 600)
endforeach()
add_shoc_test(stencil2d --customSize 32,32 --lsize 8,8 --num-iters 2 --passes 1 --warmupPasses 0)

add_shoc_test(bfs CONFIG large_sm --passes 1 --graph_file "${INPUT_DIR}/graph.metis" --algo 1)
add_shoc_test(bfs_uiuc TARGET shoc_bfs CONFIG large_sm --passes 1 --graph_file "${INPUT_DIR}/graph.metis" --algo 2)
add_shoc_test(fft --passes 1 --KiB 16)
add_shoc_test(gemm --passes 1 --dimension 64)
add_shoc_test(md CONFIG large_sm --passes 1 --atoms 256 --iterations 1)
add_shoc_test(md5hash CONFIG large_sm --passes 1 --key-length 3 --values-per-byte 8 --seed 12345 --verbose)
add_shoc_test(reduction CONFIG large_sm --passes 1 --elements 4096 --iterations 1)
add_shoc_test(scan CONFIG large_sm --passes 1 --elements 4096 --iterations 1)
add_shoc_test(sort CONFIG large_sm --passes 1 --elements 1024 --validation-input)
add_shoc_test(triad CONFIG large_sm --passes 1 --vector-kib 16 --block-kib 2)
add_shoc_test(s3d CONFIG large_sm --passes 1 --dimension 8 --dump)
# The full SP/DP run took eight minutes in coverage CI; leave runner headroom.
foreach(SM IN LISTS OPENCL_SMS)
  set_tests_properties(opencl.shoc.s3d.${SM} PROPERTIES TIMEOUT 1200)
endforeach()

foreach(DIRECTION IN ITEMS download readback)
  add_shoc_test(busspeed${DIRECTION} --max-kib 16 --passes 1)
  add_shoc_test(busspeed${DIRECTION}_unpinned
    TARGET shoc_busspeed${DIRECTION} KIND busspeed${DIRECTION}
    --max-kib 16 --passes 1 --nopinned)
endforeach()
add_shoc_test(kernelcompile --passes 1)
add_shoc_test(devicememory CONFIG large_sm
  --memory-kib 64 --memory-repeats 2 --memory-work-items 256 --memory-group-size 32 --passes 1 --quiet)
add_shoc_test(queuedelay CONFIG large_sm --groups 1 --iterations 2 --passes 1)
add_shoc_test(queuedelay_default TARGET shoc_queuedelay CONFIG large_sm --groups 1 --iterations 1)
add_shoc_test(maxflops CONFIG large_sm
  --passes 1 --elements 2048 --repeat-factor 0.15 --unroll 1 --verify --quiet)
