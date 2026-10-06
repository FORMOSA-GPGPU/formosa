/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University */
/* SPDX-License-Identifier: Apache-2.0 */

/* Constructor regression fixture for Ilha; no runtime firmware is executed. */
.section .text.entry,"ax"
.globl start
start: j start
