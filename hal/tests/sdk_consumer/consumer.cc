// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <formosa-hal/hal.h>

int main() {
  // Link and call a harmless API so the consumer verifies the complete
  // imported static-library closure without requiring an MMIO agent.
  (void)fsa_hal_is_available();
  return 0;
}
