# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

lv_script(2mm.lua)
add_kernel_sim_kernel(2mm kernel.c)  # builds 2mm.kernel-sim.elf
add_kernel_sim_test(2mm 2mm.lua)
