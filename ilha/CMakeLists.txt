# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

project(
  ilha
  VERSION 0.1
  DESCRIPTION "Lua-defined hardware architecture for FORMOSA"
  LANGUAGES CXX)

add_library(libilha INTERFACE)
target_include_directories(libilha INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(libilha INTERFACE systemc)

add_library(ilha STATIC
  src/sm_transactor.cc
  src/stub_cache_mmio.cc
  src/stub_warp_ctrl.cc
  src/wg_initializer.cc
)
target_link_libraries(ilha PUBLIC libilha liblv)
target_compile_features(ilha PUBLIC cxx_std_17)
target_compile_options(ilha PRIVATE -Wall -Wextra -Wno-unused-parameter)
lv_register_binding(ilha)

lv_script(daemon.lua)
lv_runfile(configs/apccas2026.lua)
lv_runfile(configs/large_sm.lua)
lv_runfile(configs/dram/apccas2026-lpddr4.json)
lv_runfile(configs/dram/large-sm-lpddr4.json)
lv_runfile(configs/dram/simconfig.json)

set(_dramsys_configs "${formosa_SOURCE_DIR}/third-party/DRAMSys/configs")
foreach(_cfg
    addressmapping/am_lpddr4_1Gbx16_robaco.json
    mcconfig/fr_fcfs.json
    memspec/JEDEC_1Gbx16_LPDDR4-1600.json
    memspec/JEDEC_1Gbx16_LPDDR4-3200.json)
  get_filename_component(_name "${_cfg}" NAME)
  configure_file("${_dramsys_configs}/${_cfg}"
                 "${CMAKE_BINARY_DIR}/configs/dram/${_name}" COPYONLY)
endforeach()

if(BUILD_TESTING)
  find_program(LUAJIT_EXECUTABLE NAMES luajit luajit-2.1 REQUIRED)
  add_test(NAME ilha.config
    COMMAND "${CMAKE_COMMAND}" -E env
      "LUA_PATH=${LV_LUA_RUNTIME_ROOT}/?.lua;;"
      "${LUAJIT_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/config.lua"
      "${CMAKE_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
  set_property(TEST ilha.config PROPERTY LABELS unit ilha)
  set_property(TEST ilha.config PROPERTY WORKING_DIRECTORY "/")
  set(_generator_test_header "${CMAKE_CURRENT_BINARY_DIR}/generator-header-test/formosa_addr_map.h")
  set(_generator_test_linker "${CMAKE_CURRENT_BINARY_DIR}/generator-linker-test/ilha_memory.ld")
  add_test(NAME ilha.generate_header
    COMMAND "${XMAKE_EXECUTABLE}" lua
      "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_header.lua"
      "${_generator_test_header}" "${formosa_SOURCE_DIR}"
      ilha.tests.data.addr_map_fixture)
  set_property(TEST ilha.generate_header PROPERTY FIXTURES_SETUP ilha-generator)
  add_test(NAME ilha.generate_linker
    COMMAND "${XMAKE_EXECUTABLE}" lua
      "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_linker.lua"
      "${_generator_test_linker}" "${formosa_SOURCE_DIR}"
      ilha.tests.data.addr_map_fixture)
  set_property(TEST ilha.generate_linker PROPERTY FIXTURES_SETUP ilha-generator)
  add_test(NAME ilha.platform_data
    COMMAND "${LUAJIT_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/platform_data.lua"
      "${_generator_test_header}" "${_generator_test_linker}")
  set_property(TEST ilha.platform_data PROPERTY FIXTURES_REQUIRED ilha-generator)
  set_property(TEST ilha.platform_data PROPERTY LABELS unit ilha)
  foreach(_test wg_init.lua stub_cache_mmio.lua sm_transactor.lua)
    add_lv_test_script("ilha.${_test}" "tests/unit/${_test}" LABELS unit ilha)
  endforeach()
endif()
