# Common objects for bare-metal programs.
add_library(common_objs OBJECT
  start.S
  putchar.c
  ${formosa_SOURCE_DIR}/fw/tinyprintf/tinyprintf.c
)

# Set linker script
set(LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/linker.ld PARENT_SCOPE)

target_include_directories(common_objs PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${formosa_SOURCE_DIR}/fw/tinyprintf
)
