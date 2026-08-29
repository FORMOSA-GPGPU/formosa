/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/schema.h>
#include <systemc.h>

#include <cstdint>
#include <vector>

#include "cores/sched/sched.h"

namespace simtix {

class TwoLevel : public WarpSched {
 public:
  struct Param {
    uint32_t group_size = 4;
    uint32_t timeout = 32;

    // clang-format off
    LV_SCHEMA(TwoLevel, Param,
              LV_FIELD(group_size, "Number of warps per scheduling group"),
              LV_FIELD(timeout, "Number of issues before checking the next ready group"))
    // clang-format on
  };

  explicit TwoLevel(uint32_t num_warps, uint32_t group_size = 4,
                    uint32_t timeout = 32);
  explicit TwoLevel(uint32_t num_warps, const Param &param)
      : TwoLevel(num_warps, param.group_size, param.timeout) {}
  uint32_t SelectWarp(const sc_bv_base &ready_warps) override;

 private:
  bool GroupHasReadyWarp(const sc_bv_base &ready_warps, uint32_t group) const;
  uint32_t SelectReadyWarpInGroup(const sc_bv_base &ready_warps,
                                  uint32_t group);
  void RotateActiveGroup();

  uint32_t group_size_;
  uint32_t num_groups_;
  uint32_t active_group_;
  uint32_t timeout_;
  uint32_t active_group_issues_;
  std::vector<uint32_t> prioritized_warp_;
};

}  // namespace simtix
