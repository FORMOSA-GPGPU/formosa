# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(UPSTREAM "${PROJECT_SOURCE_DIR}/tests/opencl/parboil")
set(VERIFY "${CMAKE_CURRENT_LIST_DIR}/verify.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_LIST_DIR}/generate_inputs.py")

set(INPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/inputs")
execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/generate_inputs.py" "${INPUT_DIR}"
  COMMAND_ERROR_IS_FATAL ANY)

add_library(parboil_opencl_support STATIC "${UPSTREAM}/common/src/parboil_opencl.c")
target_include_directories(parboil_opencl_support PUBLIC "${UPSTREAM}/common/include")
target_compile_definitions(parboil_opencl_support PRIVATE ${OCL_DEFAULT_OPTS})
target_link_libraries(parboil_opencl_support PUBLIC OpenCL::OpenCL m)
add_library(parboil_cpu_support STATIC "${UPSTREAM}/common/src/parboil.c")
target_include_directories(parboil_cpu_support PUBLIC "${UPSTREAM}/common/include")
target_link_libraries(parboil_cpu_support PUBLIC m)

function(add_parboil NAME VARIANT REFERENCE_VARIANT)
  cmake_parse_arguments(CASE "" "CONFIG" "SOURCES;REFERENCE_SOURCES;ARGS;EXTRA_KERNEL_FILES" ${ARGN})
  set(BENCHMARK_DIR "${UPSTREAM}/benchmarks/${NAME}")
  set(SOURCE_DIR "${BENCHMARK_DIR}/src/${VARIANT}")
  set(REFERENCE_DIR "${BENCHMARK_DIR}/src/${REFERENCE_VARIANT}")
  set(TARGET parboil_${NAME})
  set(REFERENCE ${TARGET}_reference)
  set(RUN_DIR "${CMAKE_CURRENT_BINARY_DIR}/${NAME}")

  list(TRANSFORM CASE_SOURCES PREPEND "${SOURCE_DIR}/")
  list(TRANSFORM CASE_REFERENCE_SOURCES PREPEND "${REFERENCE_DIR}/")
  add_executable(${TARGET} ${CASE_SOURCES})
  target_link_libraries(${TARGET} PRIVATE parboil_opencl_support)
  target_compile_definitions(${TARGET} PRIVATE ${OCL_DEFAULT_OPTS})
  add_executable(${REFERENCE} ${CASE_REFERENCE_SOURCES})
  target_link_libraries(${REFERENCE} PRIVATE parboil_cpu_support)
  set_target_properties(${TARGET} ${REFERENCE} PROPERTIES
    C_STANDARD 99 CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES)
  add_dependencies(${TARGET} ${REFERENCE})

  # Preserve the relative source paths the original hosts pass to OpenCL.
  file(GLOB KERNEL_FILES RELATIVE "${BENCHMARK_DIR}"
    "${SOURCE_DIR}/*.cl" "${SOURCE_DIR}/*.h")
  list(APPEND KERNEL_FILES ${CASE_EXTRA_KERNEL_FILES})
  foreach(FILE IN LISTS KERNEL_FILES)
    get_filename_component(DIRECTORY "${FILE}" DIRECTORY)
    file(MAKE_DIRECTORY "${RUN_DIR}/${DIRECTORY}")
    configure_file("${BENCHMARK_DIR}/${FILE}" "${RUN_DIR}/${FILE}" COPYONLY)
  endforeach()

  set(WRAPPER "${Python3_EXECUTABLE}" "${VERIFY}" --kind "${NAME}"
    --output result.dat --reference "$<TARGET_FILE:${REFERENCE}>")
  foreach(ARG IN LISTS CASE_ARGS)
    list(APPEND WRAPPER "--reference-arg=${ARG}")
  endforeach()
  list(APPEND WRAPPER --)
  add_lv_opencl_integration_test(${TARGET}
    ID parboil.${NAME}
    CONFIG "${CASE_CONFIG}"
    WORKING_DIRECTORY "${RUN_DIR}"
    WRAPPER ${WRAPPER}
    ARGS ${CASE_ARGS} -o result.dat)
  foreach(SM IN LISTS OPENCL_SMS)
    set_property(TEST opencl.parboil.${NAME}.${SM} APPEND PROPERTY LABELS benchmark parboil)
    set_tests_properties(opencl.parboil.${NAME}.${SM} PROPERTIES
      RESOURCE_LOCK ${TARGET} TIMEOUT 180)
  endforeach()
endfunction()

add_parboil(sgemm opencl_base base
  CONFIG large_sm
  SOURCES main.cc io.cc
  REFERENCE_SOURCES main.cc io.cc
  ARGS -i "${INPUT_DIR}/sgemm_a.txt,${INPUT_DIR}/sgemm_b.txt,${INPUT_DIR}/sgemm_bt.txt")

add_parboil(stencil opencl_base cpu
  CONFIG large_sm
  SOURCES main.c file.cc
  REFERENCE_SOURCES main.c file.c kernels.c
  ARGS -i "${INPUT_DIR}/stencil.bin" 16 4 4 2)

add_parboil(mri-q opencl cpu
  CONFIG large_sm
  SOURCES main.c file.cc computeQ.c ocl.c
  REFERENCE_SOURCES main.c file.cc
  # The upstream host's include option refers to this original header.
  EXTRA_KERNEL_FILES src/opencl_nvidia/macros.h
  ARGS -i "${INPUT_DIR}/mri-q.bin")
# main.c includes computeQ.cc; retain the original GNU C inline semantics.
target_compile_options(parboil_mri-q_reference PRIVATE
  "$<$<COMPILE_LANGUAGE:C>:-fgnu89-inline>")

add_parboil(spmv opencl_base cpu
  SOURCES main.c file.c gpu_info.c ocl.c
  REFERENCE_SOURCES main.c file.c
  ARGS -i "${INPUT_DIR}/spmv.mtx,${INPUT_DIR}/spmv-vector.bin")
set(CONVERTER "${UPSTREAM}/benchmarks/spmv/common_src/convert-dataset")
foreach(TARGET parboil_spmv parboil_spmv_reference)
  target_sources(${TARGET} PRIVATE "${CONVERTER}/convert_dataset.c" "${CONVERTER}/mmio.c")
  target_include_directories(${TARGET} PRIVATE "${CONVERTER}")
endforeach()

add_parboil(cutcp opencl_base base
  CONFIG large_sm
  SOURCES main.c cutoff.c cutcpu.c excl.c output.c readatom.c ocl.c
  REFERENCE_SOURCES main.c cutcpu.c excl.c output.c readatom.c
  ARGS -i "${INPUT_DIR}/cutcp.pqr")
