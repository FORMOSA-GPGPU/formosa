/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

namespace simtix::cache {

enum class ReplacementPolicy {
  kLRU,
  kFIFO,
  kRandom,
};

enum class WriteHitPolicy { kWriteBack, kWriteThrough };

enum class WriteMissPolicy {
  kWriteAllocate,
  kWriteNoAllocate  // aka write around
};

}  // namespace simtix::cache
