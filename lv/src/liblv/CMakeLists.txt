# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

set(TARGET liblv)

add_library(${TARGET} STATIC
  binding.cc
  common/tlm_sink.cc
  common/tlm_source.cc
  log.cc
  mm/pool.cc
  mm/pool_impl.cc
  mm/thread_safe_pool.cc
  runtime.cc
  statistics.cc
  time.cc
  trace.cc
)
set_target_properties(${TARGET} PROPERTIES OUTPUT_NAME "lv")

target_compile_definitions(${TARGET}
  PRIVATE LV_DEFAULT_RUNTIMEPATH=\"${LV_DEFAULT_RUNTIMEPATH}\"
  PRIVATE LV_DEFAULT_RUNFILES_ROOT=\"${LV_DEFAULT_RUNFILES_ROOT}\"
)

target_include_directories(${TARGET}
  PUBLIC "${lv_SOURCE_DIR}/include"
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  SYSTEM BEFORE PUBLIC ${LUAJIT_INCLUDE_DIR}
)

target_link_libraries(${TARGET}
  PUBLIC systemc fmt quill::quill tomlplusplus::tomlplusplus sol2 perfetto ${LUAJIT_LIBRARY} libcomm
)

# Compile-time maximum log level. Any LV_*/lv:: log statement below this level is
# removed entirely at compile time (no argument evaluation, no codegen) via Quill's
# QUILL_COMPILE_ACTIVE_LOG_LEVEL. error/fatal (CRITICAL) are always kept. PUBLIC
# because the logging macros expand at consumer call sites, so the value must be
# identical in every translation unit that includes <liblv/log.h>.
# Values: TRACE_L3 TRACE_L2 TRACE_L1 DEBUG INFO NOTICE WARNING ERROR CRITICAL
if(NOT LV_LOG_LEVEL)
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(LV_LOG_LEVEL "TRACE_L3")
  else()
    set(LV_LOG_LEVEL "INFO")
  endif()
endif()
set(LV_LOG_LEVEL "${LV_LOG_LEVEL}" CACHE STRING
    "Compile-time maximum log level (TRACE_L3..CRITICAL)" FORCE)
message(STATUS "LV_LOG_LEVEL=${LV_LOG_LEVEL}")
target_compile_definitions(${TARGET}
  PUBLIC QUILL_COMPILE_ACTIVE_LOG_LEVEL=QUILL_COMPILE_ACTIVE_LOG_LEVEL_${LV_LOG_LEVEL})

# Use GCOV to generate test coverages
option(LV_ENABLE_GCOV "Enable gcov support for lv" OFF)
if(LV_ENABLE_GCOV)
  message(STATUS "Enabling gcov support for lv")
  target_compile_options(${TARGET} PUBLIC --coverage)
  target_link_libraries(${TARGET} PUBLIC --coverage)
endif()

option(LV_ENABLE_PERFETTO "Enable Perfetto support for lv" OFF)
if(LV_ENABLE_PERFETTO)
  message(STATUS "Enabling Perfetto support for lv")
  target_compile_definitions(${TARGET} PUBLIC ENABLE_PERFETTO=true)
endif()
