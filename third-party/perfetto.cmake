# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

set(_perfetto_src "${CMAKE_CURRENT_LIST_DIR}/perfetto/sdk/perfetto.cc")

# perfetto's macOS clock-sync path emits an os_signpost, which relies on the
# Apple-Clang-only __builtin_os_log_format intrinsic and Apple's <os/signpost.h>
# (whose <os/trace_base.h> redeclares __dso_handle in a way GCC rejects). GCC
# therefore cannot compile perfetto.cc on macOS. Rather than patch the pinned
# submodule, on macOS + GCC generate a copy with those two Apple-only spots gated
# behind defined(__clang__) and build that instead. On Linux, and on Apple Clang,
# the upstream source is used unchanged.
if(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set(_perfetto_gen "${CMAKE_BINARY_DIR}/perfetto-gen/perfetto.cc")
  file(READ "${_perfetto_src}" _perfetto_content)
  string(REPLACE
    "#if PERFETTO_BUILDFLAG(PERFETTO_OS_MAC)\n#include <os/signpost.h>"
    "#if PERFETTO_BUILDFLAG(PERFETTO_OS_MAC) && defined(__clang__)\n#include <os/signpost.h>"
    _perfetto_content "${_perfetto_content}")
  string(REPLACE
    "#if PERFETTO_BUILDFLAG(PERFETTO_OS_MAC)\n    // Emit a MacOS point-of-interest signpost"
    "#if PERFETTO_BUILDFLAG(PERFETTO_OS_MAC) && defined(__clang__)\n    // Emit a MacOS point-of-interest signpost"
    _perfetto_content "${_perfetto_content}")
  file(WRITE "${_perfetto_gen}" "${_perfetto_content}")
  set(_perfetto_src "${_perfetto_gen}")
endif()

add_library(perfetto STATIC "${_perfetto_src}")
target_include_directories(perfetto PUBLIC "${CMAKE_CURRENT_LIST_DIR}/perfetto/sdk")
