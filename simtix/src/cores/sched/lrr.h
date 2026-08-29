/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <systemc.h>

#include "cores/sched/sched.h"

namespace simtix {

class Lrr : public WarpSched {
 public:
  explicit Lrr(uint32_t num_warps) : WarpSched(num_warps), prioritized_(0) {}
  uint32_t SelectWarp(const sc_bv_base &ready_warps) override;

 protected:
  uint32_t prioritized_;
};

}  // namespace simtix
