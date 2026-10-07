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
  set(SOURCE_DIR "${UPSTREAM}/src/opencl/level1/${DIRECTORY}")
  set(EMBEDDED "${CMAKE_CURRENT_BINARY_DIR}/${KERNEL}_cl.cpp")
  # Match config/targets.mk.in: strip CR, escape C literals, retain each LF.
  add_custom_command(OUTPUT "${EMBEDDED}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/embed_kernel.py"
      "${SOURCE_DIR}/${KERNEL}.cl" "${EMBEDDED}" "cl_source_${KERNEL}"
    DEPENDS "${SOURCE_DIR}/${KERNEL}.cl" "${CMAKE_CURRENT_LIST_DIR}/embed_kernel.py"
    VERBATIM)
  add_executable(shoc_${NAME} ${ARGN} "${EMBEDDED}" "${OPENCL_COMMON}/main.cpp")
  target_include_directories(shoc_${NAME} PRIVATE "${SOURCE_DIR}")
  target_link_libraries(shoc_${NAME} PRIVATE shoc_opencl_common)
  set_target_properties(shoc_${NAME} PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES)
endfunction()

add_shoc(spmv spmv spmv "${UPSTREAM}/src/opencl/level1/spmv/Spmv.cpp")
set(STENCIL_DIR "${UPSTREAM}/src/opencl/level1/stencil2d")
add_shoc(stencil2d stencil2d stencil2d
  "${STENCIL_DIR}/Stencil2Dmain.cpp" "${STENCIL_DIR}/OpenCLStencil.cpp"
  "${STENCIL_DIR}/OpenCLStencilFactory.cpp" "${STENCIL_DIR}/CommonOpenCLStencilFactory.cpp")
add_test(NAME opencl.shoc.validation
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/verify_test.py"
    "${CMAKE_CXX_COMPILER}" "${UPSTREAM}/src" "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(opencl.shoc.validation PROPERTIES LABELS "integration;opencl;shoc" TIMEOUT 30)

function(add_shoc_test NAME)
  cmake_parse_arguments(CASE "" "CONFIG" "" ${ARGN})
  add_lv_opencl_integration_test(shoc_${NAME}
    ID shoc.${NAME}
    CONFIG "${CASE_CONFIG}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    WRAPPER "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/verify.py" --kind "${NAME}" --
    ARGS ${CASE_UNPARSED_ARGUMENTS})
  foreach(SM IN LISTS OPENCL_SMS)
    set_property(TEST opencl.shoc.${NAME}.${SM} APPEND PROPERTY LABELS benchmark shoc)
    set_tests_properties(opencl.shoc.${NAME}.${SM} PROPERTIES TIMEOUT 180)
  endforeach()
endfunction()

add_shoc_test(spmv CONFIG large_sm --passes 1 --iterations 1 --mm_filename "${INPUT_DIR}/matrix.mtx")
add_shoc_test(stencil2d --customSize 32,32 --lsize 8,8 --num-iters 2 --passes 1 --warmupPasses 0)
