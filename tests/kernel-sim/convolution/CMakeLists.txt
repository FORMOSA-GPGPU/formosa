# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

lv_script(convolution.lua)
add_kernel_sim_kernel(convolution kernel.c)
add_kernel_sim_test(convolution convolution.lua)
