# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

macro(add_lv_unit_test LUA_SCRIPT)
  cmake_parse_arguments(LUA_SCRIPT ""
                                   ""
                                   "ARGS"
                                   ${ARGN})
  add_lv_test_script(${LUA_SCRIPT} ${LUA_SCRIPT} ARGS ${LUA_SCRIPT_ARGS} LABELS unit)
endmacro()

add_lv_unit_test(dma/dma.lua)
add_lv_unit_test(dbg/tick_agent.lua)
add_lv_unit_test(dramsys/dramsys.lua)
add_lv_unit_test(formosa/wg_init.lua)
add_lv_unit_test(formosa/stub_cache_mmio.lua)
add_lv_unit_test(simple/block_aligner.lua)
add_lv_unit_test(simple/constant_table.lua)
add_lv_unit_test(simple/clint.lua)
add_lv_unit_test(simple/exit_code_register.lua)
add_lv_unit_test(simple/memory.lua)
add_lv_unit_test(simple/print_buf.lua)
add_lv_unit_test(simple/simple.lua)
add_lv_unit_test(simple/xbar.lua)
add_lv_unit_test(systemc/time.lua)
add_lv_unit_test(systemc/signal.lua)
add_lv_unit_test(systemc/module.lua)
add_lv_unit_test(workload/elf.lua)

# Native (non-Lua) unit tests.
add_subdirectory(log)
