set(RISCV_TEST_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/link.ld)

riscv_tests_get_isa_sources(RISCV_TEST_SOURCES rv64ui
  EXCLUDE_TESTS fence_i.S ma_data.S
)
riscv_tests_get_isa_sources(RISCV_TEST_SOURCES_RV64UM rv64um)
riscv_tests_get_isa_sources(RISCV_TEST_SOURCES_RV64UZICOND rv64uzicond)
list(APPEND RISCV_TEST_SOURCES ${RISCV_TEST_SOURCES_RV64UM})
list(APPEND RISCV_TEST_SOURCES ${RISCV_TEST_SOURCES_RV64UZICOND})

list(APPEND RISCV_TEST_SOURCES ${RISCV_TEST_LOCAL_SOURCES})

set(KERNEL_SIM_RISCV_TEST_TARGETS "")

function(add_kernel_sim_riscv_test SOURCE)
  get_filename_component(TEST_NAME ${SOURCE} NAME_WE)
  get_filename_component(TEST_DIR ${SOURCE} DIRECTORY)
  get_filename_component(ISA_NAME ${TEST_DIR} NAME)
  if(TEST_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(ISA_NAME local)
  endif()

  set(TEST_ID ${ISA_NAME}.${TEST_NAME})
  set(TARGET kernel-sim.riscv-test.${TEST_ID})
  set(KERNEL_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/kernel.${TEST_ID}.S)
  set(RISCV_TEST_CALLS "    TEST(${TEST_NAME})\n")
  configure_file(kernel.S.in ${KERNEL_SOURCE} @ONLY)

  add_executable(${TARGET}
    ${KERNEL_SIM_START}
    ${KERNEL_SOURCE}
  )

  set(OBJ ${TARGET}.obj)
  add_library(${OBJ} OBJECT ${SOURCE})
  target_include_directories(${OBJ}
    BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_compile_definitions(${OBJ}
    PRIVATE TEST_FUNC_NAME=${TEST_NAME} TEST_FUNC_RET=${TEST_NAME}_ret
  )
  target_link_libraries(${OBJ} PRIVATE riscv-tests)
  target_link_libraries(${TARGET} PRIVATE ${OBJ})

  set_property(SOURCE
    ${KERNEL_SIM_START}
    ${KERNEL_SOURCE}
    ${SOURCE}
    PROPERTY LANGUAGE ASM
  )

  set_target_properties(${TARGET} PROPERTIES
    LINK_DEPENDS ${RISCV_TEST_LINKER_SCRIPT}
    OUTPUT_NAME riscv-test.${TEST_ID}.kernel-sim.elf
  )

  target_link_options(${TARGET} PRIVATE
    -fuse-ld=lld
    -static
    -Wl,--gc-sections
    -T ${RISCV_TEST_LINKER_SCRIPT}
    -nostdlib
  )

  foreach(SM IN ITEMS simtix.atomic_sm simtix.pipelined_sm)
    add_lv_test_script(kernel-sim.riscv-test.${SM}.${TEST_ID} riscv-test.lua
      LABELS kernel-sim riscv-test ${ISA_NAME}
      ARGS $<TARGET_FILE:${TARGET}> --sm ${SM}
    )
  endforeach()

  list(APPEND KERNEL_SIM_RISCV_TEST_TARGETS ${TARGET})
  set(KERNEL_SIM_RISCV_TEST_TARGETS "${KERNEL_SIM_RISCV_TEST_TARGETS}" PARENT_SCOPE)
endfunction()

foreach(SOURCE IN LISTS RISCV_TEST_SOURCES)
  add_kernel_sim_riscv_test(${SOURCE})
endforeach()

add_custom_target(kernel-sim.riscv-test
  DEPENDS ${KERNEL_SIM_RISCV_TEST_TARGETS}
)
