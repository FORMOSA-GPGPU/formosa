# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

add_library(simtix STATIC
  cache/cache.cc
  cache/cache_hazard.cc
  cache/cache_logging.cc
  cache/cache_mmio.cc
  cache/cache_watchdog.cc
  cache/mshr_file.cc
  cache/write_buffer.cc
  cache/victim_buffer.cc
  cache/data_array.cc
  cache/tag_array.cc

  cores/atomic/atomic_core.cc
  cores/pipelined/backend.cc
  cores/pipelined/frontend.cc
  cores/pipelined/frontend_tester.cc
  cores/pipelined/ghost_scheduler.cc
  cores/pipelined/ghost_scheduler_tester.cc
  cores/pipelined/packet.cc
  cores/pipelined/arbitrator/scalar_regfile.cc
  cores/pipelined/pipelined_core.cc
  cores/pipelined/scoreboard_tester.cc
  cores/pipelined/arbitrator/base.cc
  cores/pipelined/arbitrator/simple.cc
  cores/pipelined/arbitrator/pipelined.cc
  cores/pipelined/arbitrator/tester.cc
  cores/pipelined/lsu/base.cc
  cores/pipelined/lsu/tester.cc
  cores/pipelined/lsu/probe_memory.cc
  cores/pipelined/lsu/simple.cc
  cores/pipelined/lsu/coalescing.cc
  cores/pipelined/lsu/stack_remap_table.cc
  cores/scalar/packet.cc
  cores/scalar/scalar_core.cc

  cores/sched/lrr.cc
  cores/sched/gto.cc
  cores/sched/sched.cc
  cores/sched/two_level.cc
  cores/decode.cc
  cores/instr.cc
  cores/instr_amo.cc
  cores/instr_auipc.cc
  cores/instr_branch.cc
  cores/instr_formosa.cc
  cores/instr_jal.cc
  cores/instr_jalr.cc
  cores/instr_load.cc
  cores/instr_lui.cc
  cores/instr_op.cc
  cores/instr_op32.cc
  cores/instr_op_fp.cc
  cores/instr_op_fused.cc
  cores/instr_op_imm.cc
  cores/instr_op_imm32.cc
  cores/instr_store.cc
  cores/instr_system.cc

  mem/atomic_memory.cc

  utils/delay_queue_tester.cc
)
lv_register_binding(simtix)
target_include_directories(simtix PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(simtix PUBLIC liblv softfloat)

# Use GCOV to generate host-side coverage reports for simtix.
option(SIMTIX_ENABLE_GCOV "Enable gcov support for simtix" OFF)
if(SIMTIX_ENABLE_GCOV)
  message(STATUS "Enabling gcov support for simtix")
  target_compile_options(simtix PUBLIC --coverage)
  target_link_libraries(simtix PUBLIC --coverage)
endif()
