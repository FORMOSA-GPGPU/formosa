# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

lv_script(relu.lua)
add_kernel_sim_kernel(relu kernel.c)  # builds relu.kernel-sim.elf
add_kernel_sim_test(relu relu.lua)
