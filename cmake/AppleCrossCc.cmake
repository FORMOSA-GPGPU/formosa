# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

# apple_cross_cc_shim(<out-var> <real-cc>)
#
# On macOS, CMake's Darwin platform rules inject Apple host flags into every
# target regardless of the CMAKE_OSX_* variables: -arch <arch>, -isysroot <dir>
# and -mmacosx-version-min=<ver> on compiles, plus -Wl,-search_paths_first and
# -Wl,-headerpad_max_install_names on links. Subprojects that switch
# CMAKE_C_COMPILER to a bare-metal / RISC-V cross compiler must not receive these
# flags, which the cross toolchain rejects (e.g. "unsupported option '-arch'").
#
# This helper returns, in <out-var>, the path to a shim that strips those flags
# at exec time before calling <real-cc>. Because the compiler driver also invokes
# the linker, one shim covers both compiling and linking. On non-Apple hosts none
# of these flags are ever added, so <out-var> is set to <real-cc> unchanged.
function(apple_cross_cc_shim OUT_VAR REAL_CC)
  if(NOT APPLE)
    set(${OUT_VAR} "${REAL_CC}" PARENT_SCOPE)
    return()
  endif()

  string(MAKE_C_IDENTIFIER "${REAL_CC}" _id)
  set(_shim "${CMAKE_BINARY_DIR}/apple-cross-cc-shim-${_id}.sh")
  file(WRITE "${_shim}"
"#!/usr/bin/env bash
args=()
while [ \"$#\" -gt 0 ]; do
  case \"$1\" in
    -arch|-isysroot) shift 2 ;;
    -mmacosx-version-min=*|-Wl,-search_paths_first|-Wl,-headerpad_max_install_names) shift ;;
    *) args+=(\"$1\"); shift ;;
  esac
done
exec \"${REAL_CC}\" \"\${args[@]}\"
")
  file(CHMOD "${_shim}"
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
  set(${OUT_VAR} "${_shim}" PARENT_SCOPE)
endfunction()
