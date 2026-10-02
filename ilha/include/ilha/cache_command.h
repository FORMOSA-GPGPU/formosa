/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>

namespace ilha {

// Decoded cache-maintenance command; independent of the MMIO transport.
struct CacheCommand {
  uint64_t addr = 0;
  uint64_t size = 0;
  uint64_t opcode = 0;
};

}  // namespace ilha
