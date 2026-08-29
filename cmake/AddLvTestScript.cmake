# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

function(add_lv_test_script NAME LUA_SCRIPT)
  cmake_parse_arguments(LUA ""
                            "WRAPPER"
                            "ARGS;LABELS;WORKING_DIRECTORY"
                            ${ARGN})
  if (LUA_WRAPPER)
    add_test(
      NAME ${NAME}
      COMMAND ${LUA_WRAPPER} $<TARGET_FILE:lv> ${LUA_SCRIPT} ${LUA_ARGS}
    )
  else()
    add_test(
      NAME ${NAME}
      COMMAND $<TARGET_FILE:lv> ${LUA_SCRIPT} ${LUA_ARGS}
    )
  endif()

  # Set working directory
  if (LUA_WORKING_DIRECTORY)
    set_tests_properties(${NAME} PROPERTIES
      WORKING_DIRECTORY "${LUA_WORKING_DIRECTORY}"
    )
  else()
    set_tests_properties(${NAME} PROPERTIES
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
  endif()

  # Set labels
  set_tests_properties(${NAME} PROPERTIES LABELS "lv;${LUA_LABELS}")
endfunction()
