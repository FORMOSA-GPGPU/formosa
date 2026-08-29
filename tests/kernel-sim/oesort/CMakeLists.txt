# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

lv_script(oesort.lua)
add_kernel_sim_kernel(oesort kernel.c)  # builds oesort.kernel-sim.elf
add_kernel_sim_test(oesort oesort.lua)
