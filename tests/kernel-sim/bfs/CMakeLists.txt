# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

lv_script(bfs.lua)
add_kernel_sim_kernel(bfs kernel.c)  # builds bfs.kernel-sim.elf
add_kernel_sim_test(bfs bfs.lua)
