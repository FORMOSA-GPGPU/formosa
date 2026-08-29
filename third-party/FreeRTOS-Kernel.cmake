# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

get_filename_component(FREERTOS_KERNEL_CMAKE_DIR "${CMAKE_CURRENT_LIST_FILE}" PATH)

# ------------------------------------------------------------------------------
# add_freertos_instance()
#
# Create a dedicated FreeRTOS kernel instance as a CMake STATIC library target.
#
# This function allows multiple FreeRTOS instances to coexist in the same
# repository and build system by creating separate CMake targets, each with its
# own FreeRTOSConfig.h, heap implementation, port layer, and compile options.
#
# Each invocation of this function produces:
#   - One STATIC library target representing a single FreeRTOS kernel instance
#   - Independent include paths to avoid FreeRTOSConfig.h collisions
#   - Independent heap and port configuration per instance
#
# Typical use cases:
#   - Multi-core / AMP systems (one FreeRTOS instance per core)
#   - Multiple firmware images built from the same repository
#   - Safety / isolation partitions with different RTOS configurations
#
# --------------------------------------------------------------------------
# Function signature:
#
#   add_freertos_instance(<target_name>
#     [KERNEL_DIR <path>]
#     CONFIG_DIR <path>
#     PORT_DIR   <path>
#     [HEAP_FILE <path>]
#     [EXTRA_SOURCES        <src1> <src2> ...]
#     [EXTRA_INCLUDES       <inc1> <inc2> ...]
#     [EXTRA_DEFINES        <def1> <def2> ...]
#     [EXTRA_COMPILER_FLAGS <flag1> <flag2> ...]
#   )
#
# --------------------------------------------------------------------------
# Required arguments:
#
#   <target_name>
#     Name of the CMake STATIC library target to create.
#
#   CONFIG_DIR
#     Directory containing FreeRTOSConfig.h for this instance.
#     This directory is added as a PRIVATE include path to prevent
#     configuration leakage between instances.
#
#   PORT_DIR
#     Path to the FreeRTOS portable layer directory (e.g. GCC/ARM_CM4F).
#     Must contain port.c and the corresponding port header files.
#
# --------------------------------------------------------------------------
# Optional arguments:
#
#   KERNEL_DIR
#     Path to the FreeRTOS-Kernel root directory.
#     Defaults to:
#       ${CMAKE_CURRENT_SOURCE_DIR}/FreeRTOS-Kernel
#
#   HEAP_FILE
#     Heap implementation source file (e.g. heap_4.c, heap_5.c).
#     Defaults to:
#       <KERNEL_DIR>/portable/MemMang/heap_4.c
#
#   EXTRA_SOURCES
#     Additional source files to be compiled into this FreeRTOS instance
#     (e.g. application-specific RTOS extensions).
#
#   EXTRA_INCLUDES
#     Additional include directories required by this instance.
#
#   EXTRA_DEFINES
#     Additional preprocessor definitions for this instance only
#     (e.g. instance identifiers, feature toggles).
#
#   EXTRA_COMPILER_FLAGS
#     Additional compiler options applied only to this instance.
#
# --------------------------------------------------------------------------
# Notes:
#
# - One FreeRTOS instance corresponds to one CMake target.
# - Do NOT link multiple FreeRTOS instance targets into the same executable
#   unless you fully understand and handle symbol and interrupt ownership
#   conflicts.
# - This function is intended for multi-image or multi-core designs where
#   each instance is linked into a separate firmware image or core.
#
# ------------------------------------------------------------------------------
function(add_freertos_instance NAME)
  set(options)
  set(oneValueArgs
    KERNEL_DIR
    CONFIG_DIR
    PORT_DIR
    HEAP_FILE
  )
  set(multiValueArgs
    EXTRA_SOURCES
    EXTRA_INCLUDES
    EXTRA_DEFINES
    EXTRA_COMPILER_FLAGS
  )
  cmake_parse_arguments(FR "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if (NOT FR_KERNEL_DIR)
    set(FR_KERNEL_DIR "${FREERTOS_KERNEL_CMAKE_DIR}/FreeRTOS-Kernel")
    message(STATUS "add_freertos_instance(${NAME}): KERNEL_DIR not specified, using default: ${FR_KERNEL_DIR}")
  endif()
  if (NOT FR_CONFIG_DIR)
    message(FATAL_ERROR "add_freertos_instance(${NAME}): CONFIG_DIR is required")
  endif()
  if (NOT FR_PORT_DIR)
    message(FATAL_ERROR "add_freertos_instance(${NAME}): PORT_DIR is required")
  endif()
  if (NOT FR_HEAP_FILE)
    set(FR_HEAP_FILE "${FR_KERNEL_DIR}/portable/MemMang/heap_4.c")
    message(STATUS "add_freertos_instance(${NAME}): HEAP_FILE not specified, using default: ${FR_HEAP_FILE}")
  endif()

  add_library(${NAME} STATIC)

  # Core FreeRTOS source files
  target_sources(${NAME} PRIVATE
    ${FR_KERNEL_DIR}/croutine.c
    ${FR_KERNEL_DIR}/event_groups.c
    ${FR_KERNEL_DIR}/list.c
    ${FR_KERNEL_DIR}/queue.c
    ${FR_KERNEL_DIR}/stream_buffer.c
    ${FR_KERNEL_DIR}/tasks.c
    ${FR_KERNEL_DIR}/timers.c
    ${FR_HEAP_FILE}
  )

  # Port-specific source files
  target_sources(${NAME} PRIVATE
    ${FR_PORT_DIR}/port.c
    ${FR_PORT_DIR}/portASM.S
  )

  # Extra source files
  if (FR_EXTRA_SOURCES)
    target_sources(${NAME} PRIVATE ${FR_EXTRA_SOURCES})
  endif()

  # Include directories
  # - CONFIG_DIR: Directory containing FreeRTOSConfig.h
  # - Kernel/include: Core FreeRTOS headers
  # - PORT_DIR: Port-specific headers
  target_include_directories(${NAME} PUBLIC
    ${FR_CONFIG_DIR}
    ${FR_KERNEL_DIR}/include
    ${FR_PORT_DIR}
  )

  # Extra include directories
  if (FR_EXTRA_INCLUDES)
    target_include_directories(${NAME} PUBLIC ${FR_EXTRA_INCLUDES})
  endif()

  # Compiler definitions
  if (FR_EXTRA_DEFINES)
    target_compile_definitions(${NAME} PUBLIC ${FR_EXTRA_DEFINES})
  endif()

  if (FR_EXTRA_COMPILER_FLAGS)
    target_compile_options(${NAME} PUBLIC ${FR_EXTRA_COMPILER_FLAGS})
  endif()
endfunction()
