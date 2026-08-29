/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <systemc.h>

#include <cstdint>
#include <vector>

#include "cores/sched/sched.h"

namespace simtix {

class Gto : public WarpSched {
 public:
  explicit Gto(uint32_t num_warps);
  uint32_t SelectWarp(const sc_bv_base &ready_warps) override;

 protected:
  int32_t last_selected_;
  uint64_t current_time_;
  std::vector<uint64_t> last_scheduled_time_;
};

}  // namespace simtix
