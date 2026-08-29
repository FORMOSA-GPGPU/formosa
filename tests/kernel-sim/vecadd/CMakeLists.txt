# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

lv_script(vecadd.lua)
add_kernel_sim_kernel(vecadd kernel.c)  # builds vecadd.kernel-sim.elf
add_kernel_sim_test(vecadd vecadd.lua)
