# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
# SPDX-License-Identifier: Apache-2.0

find_program(CP_LUA NAMES luajit lua REQUIRED)

# The Lua generator owns test selection and startup/linker/manifest generation.
# CMake owns compilation, dependencies and the LV runfiles destination.
function(add_cp_riscv_image CORE)
  set(TEST_SOURCE_DIR "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
  set(IMAGE_DIR "${CMAKE_BINARY_DIR}/tests/cp/riscv-test")
  file(MAKE_DIRECTORY "${IMAGE_DIR}")
  file(GLOB_RECURSE CONFIG_INPUTS CONFIGURE_DEPENDS
    "${RISCV_TESTS_ISA_DIR}/*/Makefrag")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${CONFIG_INPUTS} "${TEST_SOURCE_DIR}/generate.lua"
    "${TEST_SOURCE_DIR}/start.S.in" "${TEST_SOURCE_DIR}/linker.ld.in")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env
    "LUA_PATH=${LV_LUA_RUNTIME_ROOT}/?.lua;$ENV{LUA_PATH};;"
    "${CP_LUA}" "${TEST_SOURCE_DIR}/generate.lua"
    --core "${CORE}" --riscv-tests "${RISCV_TESTS_SOURCE_DIR}" --output "${IMAGE_DIR}"
    OUTPUT_VARIABLE PLAN OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
  string(JSON WORKLOAD GET "${PLAN}" workload)
  string(JSON ISA GET "${PLAN}" isa)
  set(IMAGE "cp-riscv-${WORKLOAD}")
  if(TARGET ${IMAGE})
    return()
  endif()
  string(SUBSTRING "${ISA}" 2 2 XLEN)
  set(ARCH_FLAGS --target=riscv${XLEN} -march=${ISA})
  file(STRINGS "${IMAGE_DIR}/${WORKLOAD}.sources" TEST_SOURCES)
  add_executable(${IMAGE} "${IMAGE_DIR}/${WORKLOAD}.start.S")
  set_target_properties(${IMAGE} PROPERTIES
    OUTPUT_NAME "${WORKLOAD}" SUFFIX ".elf"
    RUNTIME_OUTPUT_DIRECTORY "${IMAGE_DIR}"
    LINK_DEPENDS "${IMAGE_DIR}/${WORKLOAD}.ld")
  target_compile_options(${IMAGE} PRIVATE ${ARCH_FLAGS})
  target_include_directories(${IMAGE} PRIVATE "${TEST_SOURCE_DIR}")
  foreach(SOURCE IN LISTS TEST_SOURCES)
    get_filename_component(NAME_WE "${SOURCE}" NAME_WE)
    set(OBJ "${IMAGE}_${NAME_WE}")
    add_library(${OBJ} OBJECT "${SOURCE}")
    target_include_directories(${OBJ} BEFORE PRIVATE "${TEST_SOURCE_DIR}")
    target_compile_options(${OBJ} PRIVATE ${ARCH_FLAGS})
    target_compile_definitions(${OBJ} PRIVATE
      TEST_FUNC_NAME=${NAME_WE} TEST_FUNC_RET=${NAME_WE}_ret TEST_FUNC_TXT="${NAME_WE}")
    target_link_libraries(${OBJ} PRIVATE riscv-tests)
    target_link_libraries(${IMAGE} PRIVATE ${OBJ})
  endforeach()
  target_link_options(${IMAGE} PRIVATE ${ARCH_FLAGS}
    -fuse-ld=lld -static -Wl,--gc-sections -T "${IMAGE_DIR}/${WORKLOAD}.ld" -nostdlib)
endfunction()
