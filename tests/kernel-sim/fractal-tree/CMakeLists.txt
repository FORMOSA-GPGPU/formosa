# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

lv_script(fractal-tree.lua)
add_kernel_sim_kernel(fractal-tree kernel.c)  # builds fractal-tree.kernel-sim.elf
add_kernel_sim_test(fractal-tree fractal-tree.lua)
