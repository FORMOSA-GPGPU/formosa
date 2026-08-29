/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef CP_STATUS_H
#define CP_STATUS_H

#include <stdbool.h>
#include <stdint.h>

void cp_firmware_status_booting(void);
void cp_firmware_status_ready(void);
void cp_firmware_status_fault(uint64_t code);
void cp_firmware_status_reset(void);
bool cp_firmware_is_ready(void);
bool cp_firmware_is_faulted(void);
uint64_t cp_firmware_boot_generation(void);

#endif
