# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

add_kernel_sim_kernel(trap kernel.c)

add_lv_test_script(kernel-sim.trap.simtix.atomic_sm trap.lua
  LABELS kernel-sim trap
  ARGS --sm simtix.atomic_sm
)

add_lv_test_script(kernel-sim.trap.simtix.pipelined_sm trap.lua
  LABELS kernel-sim trap
  ARGS --sm simtix.pipelined_sm
)
