# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
# SPDX-License-Identifier: Apache-2.0

lv_register_lua_namespace(cp "${CMAKE_CURRENT_SOURCE_DIR}")
enable_language(ASM)
set(CMAKE_ASM_COMPILER ${CLANG})

set(CP_AVAILABLE_CORES cp.command_processor)
if("simtix" IN_LIST ENABLE_PROJECTS)
  list(APPEND CP_AVAILABLE_CORES simtix.scalar_core)
endif()
set(CP_TEST_CORES "all" CACHE STRING "CPU adapter modules to test, or 'all'")
if("all" IN_LIST CP_TEST_CORES)
  set(CP_SELECTED_CORES ${CP_AVAILABLE_CORES})
else()
  set(CP_SELECTED_CORES ${CP_TEST_CORES})
endif()
foreach(CORE IN LISTS CP_SELECTED_CORES)
  if(NOT CORE IN_LIST CP_AVAILABLE_CORES)
    message(FATAL_ERROR "CPU adapter '${CORE}' is unavailable; available: ${CP_AVAILABLE_CORES}")
  endif()
endforeach()

# Every program reads the same core capabilities and platform layout.
file(GLOB_RECURSE CP_CORE_INPUTS CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/cores/*.lua")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${CP_CORE_INPUTS} "${CMAKE_CURRENT_SOURCE_DIR}/cores.lua"
  "${CMAKE_CURRENT_SOURCE_DIR}/platform.lua")

add_subdirectory(riscv-test)
add_subdirectory(interrupt)
add_subdirectory(prog0)
add_lv_test_script(cp.harness runtime_test.lua LABELS cp unit)
set_tests_properties(cp.harness PROPERTIES TIMEOUT 30)
