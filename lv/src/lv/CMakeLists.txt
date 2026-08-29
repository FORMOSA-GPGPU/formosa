# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

# Target name
set(TARGET lv)
set(TARGET_OBJ "${TARGET}_obj")

# Source files compiled into an OBJECT library to allow parallel build.
# This decouples compilation from the plugin dependency chain.
add_library(${TARGET_OBJ} OBJECT
  main.cc
  bindings/log.cc
  bindings/dbg/monitor.cc
  bindings/dbg/memory_debugger.cc
  bindings/dbg/tick_agent.cc
  bindings/dramsys/dramsys.cc
  bindings/ipc/agent.cc
  bindings/nic/banking_router.cc
  bindings/simple/block_aligner.cc
  bindings/simple/clint.cc
  bindings/simple/constant_table.cc
  bindings/simple/exit_code_register.cc
  bindings/simple/initiator.cc
  bindings/simple/memory.cc
  bindings/simple/mux.cc
  bindings/simple/outstanding_initiator.cc
  bindings/simple/perfetto_reader.cc
  bindings/simple/print_buf.cc
  bindings/simple/xbar.cc
  bindings/cp/command_processor.cc
  bindings/cp/gdbstub.cc
  bindings/cp/rv64/Regfile.cc
  bindings/cp/rv64/instr.cc
  bindings/cp/rv64/rv64.cc
  bindings/dma/dma.cc


  bindings/systemc/clock.cc
  bindings/systemc/core.cc
  bindings/systemc/module.cc
  bindings/systemc/time.cc
  bindings/systemc/signal.cc
  bindings/workload/elf.cc
  bindings/formosa/wg_initializer.cc
  bindings/formosa/sm_transactor.cc
  bindings/formosa/stub_cache_mmio.cc
  bindings/formosa/stub_warp_ctrl.cc
)

# C++ 17 is required
target_compile_features(${TARGET_OBJ}
  PUBLIC cxx_std_17)

target_include_directories(${TARGET_OBJ}
  PRIVATE
    "${formosa_SOURCE_DIR}"
    "${formosa_SOURCE_DIR}/hal/include"
    "${formosa_SOURCE_DIR}/hal/src"
)

# Compile-time dependencies
target_link_libraries(${TARGET_OBJ}
  PRIVATE
    argparse::argparse elfio::elfio
    DRAMSys::DRAMSys liblv glm::glm
)

# DRAMSys 5.6 no longer exports a DRAMSYS_RESOURCE_DIR compile definition;
# point it at the default configs shipped in the submodule.
target_compile_definitions(${TARGET_OBJ}
  PRIVATE
    DRAMSYS_RESOURCE_DIR="${formosa_SOURCE_DIR}/third-party/DRAMSys/configs"
)

# Turn on more warnings.
target_compile_options(${TARGET_OBJ}
  PRIVATE -Wall -Wextra -Wno-unused-parameter)

# Final executable definition
add_executable(${TARGET})

# Link the compiled objects and the plugins.
# Plugins are linked at this stage so they don't block parallel compilation.
target_link_libraries(${TARGET}
  PRIVATE
    ${TARGET_OBJ}
    lv_plugins
)

if(LV_ENABLE_DOCGEN)
  set(LV_META_DIR "${CMAKE_SOURCE_DIR}/.cache/lv_meta")

  find_program(LUA_LS lua-language-server)
  if(LUA_LS)
    set(LV_DOCGEN_VALIDATE_COMMAND COMMAND ${LUA_LS} --check "${LV_META_DIR}")
  endif()

  add_custom_command(
    TARGET ${TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LV_META_DIR}"
    COMMAND $<TARGET_FILE:${TARGET}> --docgen > "${LV_META_DIR}/lv_api.lua"
    ${LV_DOCGEN_VALIDATE_COMMAND}
    COMMENT "Generating and validating lv Lua meta file"
    VERBATIM
  )
endif()
