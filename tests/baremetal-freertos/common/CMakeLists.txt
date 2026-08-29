# Common objects for bare-metal programs.
add_library(common_objs OBJECT
  start.S
  putchar.c
  # Add other common source files here.
)

# Set linker script
set(LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/linker.ld PARENT_SCOPE)
