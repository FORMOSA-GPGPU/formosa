# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

# Utilities for assembling lv's live Lua runtime path.

set(LV_LUA_RUNTIME_ROOT "${CMAKE_BINARY_DIR}/lua" CACHE PATH
  "Directory containing lv Lua runtime namespaces.")
set(LV_DEFAULT_RUNTIMEPATH "${LV_LUA_RUNTIME_ROOT}" CACHE STRING
  "Default colon-separated Lua runtime roots for lv.")
set(LV_DEFAULT_RUNFILES_ROOT "${CMAKE_BINARY_DIR}" CACHE PATH
  "Default runfiles root for lv.runfile().")

function(lv_register_lua_namespace NAMESPACE SOURCE_DIR)
  if("${NAMESPACE}" STREQUAL "")
    message(FATAL_ERROR "Lua runtime namespace cannot be empty.")
  endif()

  get_filename_component(SOURCE_ABS "${SOURCE_DIR}" ABSOLUTE)
  if(NOT IS_DIRECTORY "${SOURCE_ABS}")
    return()
  endif()

  file(MAKE_DIRECTORY "${LV_LUA_RUNTIME_ROOT}")
  set(LINK_PATH "${LV_LUA_RUNTIME_ROOT}/${NAMESPACE}")

  if(IS_SYMLINK "${LINK_PATH}")
    file(REMOVE "${LINK_PATH}")
  elseif(EXISTS "${LINK_PATH}")
    message(FATAL_ERROR
      "Lua runtime namespace '${NAMESPACE}' already exists at ${LINK_PATH}, "
      "but it is not a symlink.")
  endif()

  file(RELATIVE_PATH SOURCE_REL "${LV_LUA_RUNTIME_ROOT}" "${SOURCE_ABS}")
  file(CREATE_LINK "${SOURCE_REL}" "${LINK_PATH}" SYMBOLIC RESULT CREATE_RESULT)
  if(NOT CREATE_RESULT STREQUAL "0")
    message(FATAL_ERROR
      "Failed to create Lua runtime namespace '${NAMESPACE}': ${CREATE_RESULT}")
  endif()

  message(STATUS "Lua runtime namespace ${NAMESPACE}: ${SOURCE_ABS}")
endfunction()

function(lv_register_project_lua_roots)
  foreach(PROJECT_NAME ${ARGN})
    if("${PROJECT_NAME}" STREQUAL "tests")
      continue()
    endif()
    lv_register_lua_namespace(
      "${PROJECT_NAME}"
      "${formosa_SOURCE_DIR}/${PROJECT_NAME}/lua")
  endforeach()
endfunction()

function(lv_register_test_lua_roots TESTS_ROOT)
  file(GLOB CHILDREN RELATIVE "${TESTS_ROOT}" "${TESTS_ROOT}/*")
  foreach(CHILD ${CHILDREN})
    lv_register_lua_namespace("${CHILD}" "${TESTS_ROOT}/${CHILD}/lua")
  endforeach()
endfunction()
