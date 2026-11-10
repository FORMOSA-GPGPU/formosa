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

// How a (shifted) line address is mapped to a set.
enum class SetIndexFunction {
  kModulo,  // line % num_sets
  kIpoly,   // Split polynomial remainder over GF(2)
};

}  // namespace simtix::cache
