# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.30...4.1)

project(
  formosa
  VERSION 0.1
  LANGUAGES CXX C)

include(GNUInstallDirs)

# Always dump compile_commands.json.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Setup output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Set default build type to Debug.
if(NOT CMAKE_BUILD_TYPE)
  message(STATUS "CMAKE_BUILD_TYPE not specified, default is 'Debug'")
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build" FORCE)
else()
  message(STATUS "CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
endif()

include(CTest)
enable_testing()
include(cmake/LvScript.cmake)
include(cmake/LvBinding.cmake)
include(cmake/AddLvTestScript.cmake)
include(cmake/LvRunfile.cmake)
include(cmake/LvRuntimeLua.cmake)
include(cmake/CheckAVX.cmake)
include(cmake/CheckCUDA.cmake)
include(cmake/AppleCrossCc.cmake)

# On macOS, CMake's find_* commands prefer Apple frameworks first, so
# find_package(OpenCL) would resolve to the deprecated system OpenCL.framework
# (old 1.2 headers that do not compile here, and which bypass the simulator).
# This project targets the ocl-icd loader, which dispatches to the FORMOSA pocl
# ICD via OCL_ICD_VENDORS. Deprioritise frameworks so find_package instead picks
# up ocl-icd's libOpenCL and the Khronos headers provided by the dev shell.
if(APPLE)
  set(CMAKE_FIND_FRAMEWORK LAST)
endif()

###################
# Enable projects #
###################
set(ALL_PROJECTS "hal;libcomm;lv;simtix;tests;fw")
set(ENABLE_PROJECTS "" CACHE STRING
    "Semicolon-separated list of projects to build (${ALL_PROJECTS}), or \"all\".")

# First expand "all" to ALL_PROJECTS.
if(ENABLE_PROJECTS STREQUAL "all")
  set(ENABLE_PROJECTS ${ALL_PROJECTS})
endif()

# Make sure enabled projects are valid.
foreach(proj ${ENABLE_PROJECTS})
  if (NOT "${proj}" IN_LIST ALL_PROJECTS)
    MESSAGE(FATAL_ERROR "${proj} isn't a known project: ${ALL_PROJECTS}.")
  endif()
endforeach()

# Third-party dependencies to build.
set(ENABLE_THIRD_PARTIES "")

############################
# Consolidate dependencies #
############################
set(DEPENDENCY_RESOLVED FALSE)

# A depends on B.
# The DEPENDENCY_TYPE can be either SUBPROJECT or THIRD_PARTY.
macro(make_dependency A DEPENDENCY_TYPE B)
  if("${DEPENDENCY_TYPE}" STREQUAL "SUBPROJECT")
    set(ENABLE_LIST ENABLE_PROJECTS)
  elseif ("${DEPENDENCY_TYPE}" STREQUAL "THIRD_PARTY")
    set(ENABLE_LIST ENABLE_THIRD_PARTIES)
  endif()

  if("${A}" IN_LIST ENABLE_PROJECTS AND NOT "${B}" IN_LIST ${ENABLE_LIST})
    # Only print the information when it's a subproject dependency.
    if("${DEPENDENCY_TYPE}" STREQUAL "SUBPROJECT")
      message(STATUS "Enabling ${B} as a dependency of ${A}")
    endif()
    list(APPEND ${ENABLE_LIST} "${B}")
    set(DEPENDENCY_RESOLVED FALSE)  # Loop again
  endif()
endmacro()

while (NOT DEPENDENCY_RESOLVED)
  set(DEPENDENCY_RESOLVED TRUE)
  make_dependency(hal SUBPROJECT libcomm)
  make_dependency(hal SUBPROJECT fw)
  make_dependency(simtix THIRD_PARTY fmt)
  make_dependency(simtix THIRD_PARTY softfloat)
  if(SIMTIX_ENABLE_TESTING)
    make_dependency(simtix THIRD_PARTY Catch2)
  endif()
  make_dependency(simtix SUBPROJECT lv)
  make_dependency(lv SUBPROJECT libcomm)
  make_dependency(lv THIRD_PARTY argparse)
  make_dependency(lv THIRD_PARTY elfio)
  make_dependency(lv THIRD_PARTY fmt)
  make_dependency(lv THIRD_PARTY quill)
  make_dependency(lv THIRD_PARTY tomlplusplus)
  make_dependency(lv THIRD_PARTY perfetto)
  make_dependency(lv THIRD_PARTY sol2)
  make_dependency(lv THIRD_PARTY systemc)
  make_dependency(lv THIRD_PARTY DRAMSys)  # Put this after systemc to avoid DRAMSys's FetchContent
  make_dependency(lv THIRD_PARTY glm)
  make_dependency(lv THIRD_PARTY stb)
  make_dependency(tests THIRD_PARTY riscv-tests)
  # TODO: Declare dependencies here.
endwhile()

lv_register_project_lua_roots(${ENABLE_PROJECTS})

function(formosa_install_target TARGET)
  install(TARGETS ${TARGET}
    EXPORT FormosaTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )
  set_property(GLOBAL PROPERTY PROJECT_HAS_EXPORTS ON)
endfunction()

# Don't build shared lib for third-party libraries.
set(BUILD_SHARED_LIBS FALSE)
foreach(proj ${ENABLE_THIRD_PARTIES})
  message(STATUS "Enabling third-party: ${proj}")
  set(THIRD_PARTY_PATH "${formosa_SOURCE_DIR}/third-party/${proj}")
  if(EXISTS ${THIRD_PARTY_PATH}.cmake)
    include(${THIRD_PARTY_PATH}.cmake)
  else()
    add_subdirectory(${THIRD_PARTY_PATH})
  endif()
endforeach()

foreach(proj ${ENABLE_PROJECTS})
  message(STATUS "Enabling subproject: ${proj}")
  add_subdirectory(${proj})
endforeach()

# Cache the results so that subsequent runs remember what are enabled.
set(ENABLE_PROJECTS ${ENABLE_PROJECTS} CACHE STRING
    "Semicolon-separated list of projects to build (${ALL_PROJECTS}), or \"all\"." FORCE)

get_property(HAS_EXPORT GLOBAL PROPERTY PROJECT_HAS_EXPORTS)
if(HAS_EXPORT)
  install(
    EXPORT FormosaTargets
    FILE FormosaTargets.cmake
    NAMESPACE Formosa::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Formosa)

  include(CMakePackageConfigHelpers)

  write_basic_package_version_file(
    "FormosaConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
  )

  configure_package_config_file(
    "FormosaConfig.cmake.in"
    "FormosaConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Formosa
  )

  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/FormosaConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/FormosaConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Formosa
  )
endif()
