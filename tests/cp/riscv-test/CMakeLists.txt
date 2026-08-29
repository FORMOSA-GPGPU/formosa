cmake_minimum_required(VERSION 3.10)

project(riscv-test)
enable_language(ASM)

set(TARGET riscv-test.elf)

# Using FORMOSA LLVM's clang in the nix shell for now.
set(CMAKE_C_COMPILER ${CLANG})
set(CMAKE_ASM_COMPILER ${CMAKE_C_COMPILER})

set(CMAKE_C_FLAGS "--target=riscv64 -march=rv64im")
set(CMAKE_ASM_FLAGS "--target=riscv64 -march=rv64im")

riscv_tests_get_isa_sources(RISCV_TEST_SOURCES rv64ui
  EXCLUDE_TESTS fence_i.S ma_data.S
)
riscv_tests_get_isa_sources(RISCV_TEST_SOURCES_RV64UM rv64um)
list(APPEND RISCV_TEST_SOURCES ${RISCV_TEST_SOURCES_RV64UM})

set(SRC
  "${CMAKE_CURRENT_SOURCE_DIR}/start.S"
  ${RISCV_TEST_SOURCES}
)

set_property(SOURCE ${SRC} PROPERTY LANGUAGE ASM)

set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld)

add_executable(${TARGET})

foreach(SOURCE ${SRC})
  get_filename_component(NAME_WE ${SOURCE} NAME_WE)
  set(OBJ ${NAME_WE}_OBJ)
  add_library(${OBJ} OBJECT ${SOURCE})
  target_include_directories(${OBJ}
    BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_compile_definitions(${OBJ}
    PRIVATE TEST_FUNC_NAME=${NAME_WE} TEST_FUNC_RET=${NAME_WE}_ret TEST_FUNC_TXT=\"${NAME_WE}\")
  target_link_libraries(${OBJ} PRIVATE riscv-tests)
  target_link_libraries(${TARGET} PRIVATE ${OBJ})
endforeach()

set_target_properties(${TARGET} PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})

target_link_options(${TARGET} PRIVATE
  -fuse-ld=lld -static -Wl,--gc-sections -T ${LINKER_SCRIPT} -static -nostdlib)
