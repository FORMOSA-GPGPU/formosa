/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <systemc.h>

#include <cstdint>

namespace simtix {

class WarpSched {
 public:
  explicit WarpSched(uint32_t num_warps) : num_warps_(num_warps) {}
  virtual ~WarpSched() = default;
  virtual uint32_t SelectWarp(const sc_bv_base &ready_warps) = 0;

 protected:
  const uint32_t num_warps_;
};

}  // namespace simtix
