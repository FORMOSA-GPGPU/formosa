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
  };

  Op op = Op::kAdd;

  tlm_extension_base *clone() const override {
    auto *ext = new AtomicExtension;
    ext->op = op;
    return ext;
  }

  void copy_from(const tlm_extension_base &other) override {
    op = static_cast<const AtomicExtension &>(other).op;
  }
};

}  // namespace simtix
