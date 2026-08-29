# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

function(lv_script RELATIVE_SCRIPT_PATH)
  # 1. Resolve the full path of the input and output files
  get_filename_component(SCRIPT_FILENAME "${RELATIVE_SCRIPT_PATH}" NAME)
  set(INPUT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/${RELATIVE_SCRIPT_PATH}")

  # Ensure output directory is defined, default to binary dir if not
  if(DEFINED CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(OUTPUT_FILE "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${SCRIPT_FILENAME}")
  else()
    set(OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/${SCRIPT_FILENAME}")
  endif()

  # 2. Create the build-time command
  # We use a unique target name based on the filename to avoid conflicts
  string(MAKE_C_IDENTIFIER "patch_${SCRIPT_FILENAME}" TARGET_ID)

  add_custom_command(
    OUTPUT "${OUTPUT_FILE}"
    # Step A: Echo the shebang line into the new file (overwrite)
    # We use generator expression $<TARGET_FILE> to get the absolute path
    COMMAND echo "\#!/usr/bin/env $<TARGET_FILE:lv>" > "${OUTPUT_FILE}"

    # Step B: Append the content of the original lua script
    COMMAND cat "${INPUT_FILE}" >> "${OUTPUT_FILE}"

    # Step C: Make the file executable
    COMMAND chmod +x "${OUTPUT_FILE}"

    # Step D: dependency tracking.
    # If INPUT_FILE changes, this command re-runs.
    DEPENDS "${INPUT_FILE}"

    COMMENT "Patching and deploying Lua script: ${SCRIPT_FILENAME}"
    VERBATIM
  )

  # 3. Create a custom target to drive the command
  # The 'ALL' keyword ensures this runs whenever you build the project
  add_custom_target(${TARGET_ID} ALL DEPENDS "${OUTPUT_FILE}")

  # Ensure the main binary is built before the script is patched
  # (Useful if the script execution depends on the binary existing immediately)
  add_dependencies(${TARGET_ID} lv)
endfunction()
