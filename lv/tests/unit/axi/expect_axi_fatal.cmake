# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

execute_process(
  COMMAND "${PROGRAM}" --axi-safety "${CASE}" "${SPLIT}" "${READ}" "${BEATS}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 15
)
set(log "${output}\n${error}")
if(NOT result STREQUAL "23" OR NOT log MATCHES "${EXPECTED_MESSAGE}"
    OR log MATCHES "runtime error:|UndefinedBehaviorSanitizer|AddressSanitizer|TIMEOUT")
  message(FATAL_ERROR "Expected fatal matching ${EXPECTED_MESSAGE}, got ${result}:\n${log}")
endif()
