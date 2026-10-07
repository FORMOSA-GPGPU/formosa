# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

include_guard(GLOBAL)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(OpenMP REQUIRED COMPONENTS C CXX)

add_library(rodinia_readback SHARED "${CMAKE_CURRENT_LIST_DIR}/readback.c")
target_include_directories(rodinia_readback PRIVATE "${OpenCL_INCLUDE_DIRS}")
target_compile_definitions(rodinia_readback PRIVATE ${OCL_DEFAULT_OPTS})
target_link_libraries(rodinia_readback PRIVATE ${CMAKE_DL_LIBS})

function(add_rodinia TARGET)
  add_executable(${TARGET} ${ARGN})
  target_link_libraries(${TARGET} PRIVATE OpenCL::OpenCL m)
  target_compile_definitions(${TARGET} PRIVATE ${OCL_DEFAULT_OPTS})
  target_include_directories(${TARGET} PRIVATE
    "${PROJECT_SOURCE_DIR}/tests/opencl/rodinia/opencl/util")
  # The original hosts predate C++17's std::data.
  set_target_properties(${TARGET} PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES)
endfunction()

function(stage_rodinia_files NAME DIRECTORY)
  set(UPSTREAM "${PROJECT_SOURCE_DIR}/tests/opencl/rodinia")
  set(RUN_DIR "${CMAKE_CURRENT_BINARY_DIR}/${NAME}")
  file(MAKE_DIRECTORY "${RUN_DIR}")
  file(GLOB_RECURSE RUN_FILES "${UPSTREAM}/opencl/${DIRECTORY}/*.cl"
    "${UPSTREAM}/opencl/${DIRECTORY}/*.h")
  foreach(SOURCE IN LISTS RUN_FILES)
    file(RELATIVE_PATH FILENAME "${UPSTREAM}/opencl/${DIRECTORY}" "${SOURCE}")
    configure_file("${SOURCE}" "${RUN_DIR}/${FILENAME}" COPYONLY)
  endforeach()
endfunction()

function(add_rodinia_test NAME DIRECTORY)
  cmake_parse_arguments(CASE "READBACK" "PROGRAM;ID;SUFFIX;KIND;REFERENCE;OUTPUT;CONFIG"
    "ARGS;REFERENCE_ARGS" ${ARGN})
  set(SUPPORT "${PROJECT_SOURCE_DIR}/tests/opencl/rodinia")
  set(RUN_DIR "${CMAKE_CURRENT_BINARY_DIR}/${NAME}")
  stage_rodinia_files("${NAME}" "${DIRECTORY}")
  if(NOT CASE_KIND)
    set(CASE_KIND "${NAME}")
  endif()
  if(NOT CASE_PROGRAM)
    set(CASE_PROGRAM rodinia_${NAME})
  endif()
  if(NOT CASE_ID)
    set(CASE_ID rodinia.${NAME})
  endif()
  if(CASE_SUFFIX)
    string(APPEND CASE_ID "_${CASE_SUFFIX}")
  endif()
  set(WRAPPER "${Python3_EXECUTABLE}" "${SUPPORT}/verify.py" --kind "${CASE_KIND}")
  if(CASE_READBACK)
    list(APPEND WRAPPER --readback-library "$<TARGET_FILE:rodinia_readback>")
    add_dependencies(${CASE_PROGRAM} rodinia_readback)
  endif()
  if(CASE_OUTPUT)
    list(APPEND WRAPPER --output "${CASE_OUTPUT}")
  endif()
  if(CASE_REFERENCE)
    list(APPEND WRAPPER --reference "$<TARGET_FILE:${CASE_REFERENCE}>")
  endif()
  foreach(ARG IN LISTS CASE_REFERENCE_ARGS)
    list(APPEND WRAPPER "--reference-arg=${ARG}")
  endforeach()
  list(APPEND WRAPPER --)
  add_lv_opencl_integration_test(${CASE_PROGRAM}
    ID "${CASE_ID}"
    CONFIG "${CASE_CONFIG}"
    WORKING_DIRECTORY "${RUN_DIR}"
    WRAPPER ${WRAPPER} ARGS ${CASE_ARGS})
  foreach(SM IN LISTS OPENCL_SMS)
    set_property(TEST opencl.${CASE_ID}.${SM} APPEND PROPERTY LABELS benchmark rodinia)
    # Upstream hosts can write fixed filenames. Each dataset gets its own cwd.
    set_tests_properties(opencl.${CASE_ID}.${SM} PROPERTIES
      RESOURCE_LOCK rodinia_${NAME} TIMEOUT 300)
  endforeach()
endfunction()
