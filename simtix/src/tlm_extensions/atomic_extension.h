/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <tlm.h>

#include <cstdint>

namespace simtix {

struct AtomicExtension : tlm::tlm_extension<AtomicExtension> {
  enum class Op : uint8_t {
    kSwap = 0,
    kAdd,
    kXor,
    kAnd,
    kOr,
    kMin,
    kMax,
    kMinU,
    kMaxU,
    kCompareSwap,
  };

  Op op = Op::kAdd;
  uint64_t compare_value = 0;

  tlm_extension_base *clone() const override {
    auto *ext = new AtomicExtension;
    ext->copy_from(*this);
    return ext;
  }

  void copy_from(const tlm_extension_base &other) override {
    op = static_cast<const AtomicExtension &>(other).op;
    compare_value = static_cast<const AtomicExtension &>(other).compare_value;
  }
};

}  // namespace simtix
