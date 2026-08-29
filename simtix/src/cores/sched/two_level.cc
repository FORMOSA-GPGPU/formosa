// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/sched/two_level.h"

#include <liblv/binding.h>

#include <algorithm>

#include "cores/param.h"

namespace simtix {

TwoLevel::TwoLevel(uint32_t num_warps, uint32_t group_size, uint32_t timeout)
    : WarpSched(num_warps),
      group_size_(std::max(1U, group_size)),
      num_groups_((num_warps + group_size_ - 1) / group_size_),
      active_group_(0),
      timeout_(std::max(1U, timeout)),
      active_group_issues_(0),
      prioritized_warp_(num_groups_, 0) {}

bool TwoLevel::GroupHasReadyWarp(const sc_bv_base &ready_warps,
                                 uint32_t group) const {
  uint32_t start = group * group_size_;
  uint32_t end = std::min(start + group_size_, num_warps_);
  for (uint32_t wid = start; wid < end; ++wid) {
    if (ready_warps[wid] == 1) return true;
  }
  return false;
}

uint32_t TwoLevel::SelectReadyWarpInGroup(const sc_bv_base &ready_warps,
                                          uint32_t group) {
  uint32_t start = group * group_size_;
  uint32_t end = std::min(start + group_size_, num_warps_);
  uint32_t group_warps = end - start;

  for (uint32_t i = 0; i < group_warps; ++i) {
    uint32_t offset = (prioritized_warp_[group] + i) % group_warps;
    uint32_t wid = start + offset;
    if (ready_warps[wid] == 1) {
      prioritized_warp_[group] = (offset + 1) % group_warps;
      return wid;
    }
  }

  return -1;
}

void TwoLevel::RotateActiveGroup() {
  active_group_ = (active_group_ + 1) % num_groups_;
  active_group_issues_ = 0;
}

uint32_t TwoLevel::SelectWarp(const sc_bv_base &ready_warps) {
  assert(ready_warps != 0);

  if (GroupHasReadyWarp(ready_warps, active_group_)) {
    if (active_group_issues_ >= timeout_) {
      for (uint32_t g = 1; g < num_groups_; ++g) {
        uint32_t next_group = (active_group_ + g) % num_groups_;
        if (GroupHasReadyWarp(ready_warps, next_group)) {
          active_group_ = next_group;
          active_group_issues_ = 0;
          break;
        }
      }
    }
  } else {
    for (uint32_t g = 1; g <= num_groups_; ++g) {
      RotateActiveGroup();
      if (GroupHasReadyWarp(ready_warps, active_group_)) break;
    }
  }

  uint32_t selected = SelectReadyWarpInGroup(ready_warps, active_group_);
  assert(selected != (uint32_t)-1);
  ++active_group_issues_;
  return selected;
}

LV_BINDING_WITH_BASES(simtix, TwoLevel, WarpSched)
    .constructor(
        [](const ArchParam &param) {
          return std::make_shared<TwoLevel>(param.num_warps);
        },
        lv::params("param"), lv::doc("Create a two-level scheduler"))
    .constructor(
        [](const ArchParam &param, const TwoLevel::Param &sched_param) {
          return std::make_shared<TwoLevel>(param.num_warps, sched_param);
        },
        lv::params("param", "sched_param"),
        lv::doc("Create a two-level scheduler"));

}  // namespace simtix
