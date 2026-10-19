/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cp_status.h"

#include "cp_defs.h"

static uint64_t boot_generation;
static FsaFirmwareStatus status = kFsaFirmwareStatusReset;

static volatile struct cp_mmio *cp_status_mmio(void) {
  return (volatile struct cp_mmio *)CP_BASE;
}

void cp_firmware_status_booting(void) {
  volatile struct cp_mmio *cp = cp_status_mmio();
  const uint64_t previous_generation = cp->FW_BOOT_GENERATION;
  boot_generation =
      previous_generation == UINT64_MAX ? 1 : previous_generation + 1;
  if (boot_generation == 0) boot_generation = 1;
  cp->FW_BOOT_GENERATION = boot_generation;
  cp->FW_ABI_VERSION = FSA_COMMAND_ABI_VERSION_V3;
  cp->FW_FAULT_CODE = kFsaFirmwareFaultNone;
  cp->FW_STATUS = kFsaFirmwareStatusBooting;
  status = kFsaFirmwareStatusBooting;
  asm volatile("fence w, w" ::: "memory");
}

void cp_firmware_status_ready(void) {
  volatile struct cp_mmio *cp = cp_status_mmio();
  cp->FW_ABI_VERSION = FSA_COMMAND_ABI_VERSION_V3;
  cp->FW_BOOT_GENERATION = boot_generation;
  cp->FW_FAULT_CODE = kFsaFirmwareFaultNone;
  /* Publish READY last. */
  cp->FW_STATUS = kFsaFirmwareStatusReady;
  status = kFsaFirmwareStatusReady;
  asm volatile("fence w, w" ::: "memory");
}

void cp_firmware_status_fault(uint64_t code) {
  volatile struct cp_mmio *cp = cp_status_mmio();
  cp->FW_FAULT_CODE = code;
  cp->FW_STATUS = kFsaFirmwareStatusFault;
  status = kFsaFirmwareStatusFault;
  asm volatile("fence w, w" ::: "memory");
}

void cp_firmware_status_reset(void) {
  volatile struct cp_mmio *cp = cp_status_mmio();
  cp->FW_STATUS = kFsaFirmwareStatusReset;
  status = kFsaFirmwareStatusReset;
  asm volatile("fence w, w" ::: "memory");
}

bool cp_firmware_is_ready(void) { return status == kFsaFirmwareStatusReady; }

bool cp_firmware_is_faulted(void) { return status == kFsaFirmwareStatusFault; }

uint64_t cp_firmware_boot_generation(void) { return boot_generation; }
