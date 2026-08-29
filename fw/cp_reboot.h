/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef CP_REBOOT_H
#define CP_REBOOT_H

#include <stdbool.h>

void cp_reboot_request_from_isr(void);
bool cp_reboot_is_requested(void);
/* Drain active work, clear ring, publish Reset, jump ROM. Never returns. */
void cp_reboot_perform(void);

#endif
