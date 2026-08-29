# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

set(RISCV_TESTS_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/riscv-tests")
set(RISCV_TESTS_ISA_DIR "${RISCV_TESTS_SOURCE_DIR}/isa")

if(NOT EXISTS "${RISCV_TESTS_ISA_DIR}/rv64ui/simple.S")
  message(FATAL_ERROR
    "third-party/riscv-tests is missing. Run: git submodule update --init third-party/riscv-tests")
endif()

# Public API: returns test names listed by an ISA Makefrag's *_sc_tests variable.
# Example: riscv_tests_get_isa_tests(TEST_NAMES rv64ui)
function(riscv_tests_get_isa_tests OUT_VAR ISA_DIR)
  set(MAKEFRAG "${RISCV_TESTS_ISA_DIR}/${ISA_DIR}/Makefrag")
  if(NOT EXISTS "${MAKEFRAG}")
    message(FATAL_ERROR "riscv-tests ISA Makefrag is missing: ${MAKEFRAG}")
  endif()

  file(STRINGS "${MAKEFRAG}" MAKEFRAG_LINES)
  set(TEST_VAR "${ISA_DIR}_sc_tests")
  set(COLLECTING FALSE)
  set(TEST_NAMES)

  foreach(LINE IN LISTS MAKEFRAG_LINES)
    string(STRIP "${LINE}" LINE)
    if(NOT COLLECTING)
      if(LINE MATCHES "^${TEST_VAR}[ \t]*=")
        set(COLLECTING TRUE)
        string(REGEX REPLACE "^${TEST_VAR}[ \t]*=[ \t]*" "" LINE "${LINE}")
      else()
        continue()
      endif()
    elseif(LINE MATCHES "^[A-Za-z0-9_]+[ \t]*=")
      break()
    endif()

    # Makefrag test lists are backslash-continued make variables. Parse only
    # the scalar test-name tokens and preserve the Makefrag order.
    if(LINE MATCHES "\\\\$")
      set(CONTINUES TRUE)
    else()
      set(CONTINUES FALSE)
    endif()

    string(REGEX REPLACE "\\\\$" "" LINE "${LINE}")
    string(STRIP "${LINE}" LINE)
    if(LINE)
      string(REGEX MATCHALL "[A-Za-z0-9_]+" LINE_TEST_NAMES "${LINE}")
      list(APPEND TEST_NAMES ${LINE_TEST_NAMES})
    endif()

    if(NOT CONTINUES)
      break()
    endif()
  endforeach()

  if(NOT TEST_NAMES)
    message(FATAL_ERROR "No ${TEST_VAR} entries found in ${MAKEFRAG}")
  endif()

  set(${OUT_VAR} ${TEST_NAMES} PARENT_SCOPE)
endfunction()

# Public API: returns absolute .S source paths for an ISA directory.
# With no TESTS argument, this returns all tests listed in ISA_DIR/Makefrag.
# EXCLUDE_TESTS accepts either test names or .S filenames.
# Example: riscv_tests_get_isa_sources(SOURCES rv64ui TESTS simple add)
# Example: riscv_tests_get_isa_sources(SOURCES rv64ui EXCLUDE_TESTS fence_i.S)
function(riscv_tests_get_isa_sources OUT_VAR ISA_DIR)
  cmake_parse_arguments(RVTESTS "" "" "TESTS;EXCLUDE_TESTS" ${ARGN})
  if(RVTESTS_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR
      "riscv_tests_get_isa_sources got unexpected arguments: ${RVTESTS_UNPARSED_ARGUMENTS}")
  endif()

  if(RVTESTS_TESTS)
    set(TEST_NAMES ${RVTESTS_TESTS})
  else()
    riscv_tests_get_isa_tests(TEST_NAMES ${ISA_DIR})
  endif()

  if(RVTESTS_EXCLUDE_TESTS)
    set(EXCLUDE_TEST_NAMES)
    foreach(EXCLUDE_TEST IN LISTS RVTESTS_EXCLUDE_TESTS)
      get_filename_component(EXCLUDE_TEST_NAME "${EXCLUDE_TEST}" NAME_WE)
      set(EXCLUDE_SOURCE "${RISCV_TESTS_ISA_DIR}/${ISA_DIR}/${EXCLUDE_TEST_NAME}.S")
      if(NOT EXISTS "${EXCLUDE_SOURCE}")
        message(FATAL_ERROR "riscv-tests excluded source is missing: ${EXCLUDE_SOURCE}")
      endif()
      list(APPEND EXCLUDE_TEST_NAMES "${EXCLUDE_TEST_NAME}")
    endforeach()
    list(REMOVE_ITEM TEST_NAMES ${EXCLUDE_TEST_NAMES})
  endif()

  set(SOURCES)
  foreach(TEST_NAME IN LISTS TEST_NAMES)
    set(SOURCE "${RISCV_TESTS_ISA_DIR}/${ISA_DIR}/${TEST_NAME}.S")
    if(NOT EXISTS "${SOURCE}")
      message(FATAL_ERROR "riscv-tests source is missing: ${SOURCE}")
    endif()
    list(APPEND SOURCES "${SOURCE}")
  endforeach()

  set(${OUT_VAR} ${SOURCES} PARENT_SCOPE)
endfunction()

if(NOT TARGET riscv-tests)
  add_library(riscv-tests INTERFACE)
  target_include_directories(riscv-tests INTERFACE
    "${RISCV_TESTS_ISA_DIR}/macros/scalar"
  )
endif()
