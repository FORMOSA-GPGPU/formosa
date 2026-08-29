# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

lv_script(vecmul.lua)
add_kernel_sim_kernel(vecmul kernel.c)  # builds vecmul.kernel-sim.elf
add_kernel_sim_test(vecmul vecmul.lua)
