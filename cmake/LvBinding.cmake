# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

# Target-based utilities for Lunaverse bindings.

# Define global interface targets for lv plugins.
if(NOT TARGET lv_plugins)
  add_library(lv_plugins INTERFACE)
endif()

# A function to register a binding library to the global plugin interfaces.
# This uses CMake 3.24+ $<LINK_LIBRARY:WHOLE_ARCHIVE,target> generator expression.
function(lv_register_binding NAME)
  if(NOT "lv" IN_LIST ENABLE_PROJECTS)
    return()
  endif()

  if(NOT TARGET ${NAME})
    message(FATAL_ERROR "Target '${NAME}' does not exist. Cannot register as an lv binding.")
  endif()

  # Register to the global interface with whole-archive linking
  target_link_libraries(lv_plugins INTERFACE $<LINK_LIBRARY:WHOLE_ARCHIVE,${NAME}>)
endfunction()
