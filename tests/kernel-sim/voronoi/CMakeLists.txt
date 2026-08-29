# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

lv_script(voronoi.lua)
add_kernel_sim_kernel(voronoi kernel.c)  # builds voronoi.kernel-sim.elf
add_kernel_sim_test(voronoi voronoi.lua)
