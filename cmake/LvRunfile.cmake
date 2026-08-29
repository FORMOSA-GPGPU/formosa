# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

# Copy files to the build directory preserving their relative paths from project root.
# This ensures that Lua scripts can use runfile("path/relative/to/project/root")
# to access the files consistently.
function(lv_runfile)
  foreach(FILE_PATH ${ARGV})
    # Resolve the absolute path of the input file
    get_filename_component(INPUT_ABS_PATH "${FILE_PATH}" ABSOLUTE)

    # Determine the path relative to the project root
    file(RELATIVE_PATH REL_PATH "${PROJECT_SOURCE_DIR}" "${INPUT_ABS_PATH}")

    # Set the output path in the binary directory, preserving the relative structure
    set(OUTPUT_ABS_PATH "${CMAKE_BINARY_DIR}/${REL_PATH}")

    # Use configure_file(COPYONLY) to copy the file at configure time
    # This is consistent with the existing mechanism used in the project.
    configure_file("${INPUT_ABS_PATH}" "${OUTPUT_ABS_PATH}" COPYONLY)
  endforeach()
endfunction()
