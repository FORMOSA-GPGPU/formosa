// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/sched/gto.h"

#include <liblv/binding.h>

#include "cores/param.h"

namespace simtix {

Gto::Gto(uint32_t num_warps)
    : WarpSched(num_warps), last_selected_(-1), current_time_(0) {
  /* Initialize all timestamps to 0 */
  last_scheduled_time_.resize(num_warps_, 0);
}

uint32_t Gto::SelectWarp(const sc_bv_base &ready_warps) {
  assert(ready_warps != 0);

  /* Greedy: check if last selected warp is still ready */
  if (last_selected_ != -1 && ready_warps[last_selected_] == 1) {
    last_scheduled_time_[last_selected_] = ++current_time_;
    return (uint32_t)last_selected_;
  }

  /* Oldest: search for the ready warp with the smallest timestamp */
  uint32_t oldest_ready = -1;
  uint64_t oldest_time = UINT64_MAX;

  for (uint32_t i = 0; i < num_warps_; ++i) {
    if (ready_warps[i] == 1 && last_scheduled_time_[i] < oldest_time) {
      oldest_time = last_scheduled_time_[i];
      oldest_ready = i;
    }
  }

  if (oldest_ready != (uint32_t)-1) {
    last_selected_ = oldest_ready;
    last_scheduled_time_[oldest_ready] = ++current_time_;
    return oldest_ready;
  }

  return -1;
}

LV_BINDING_WITH_BASES(simtix, Gto, WarpSched)
    .constructor(
        [](const ArchParam &param) {
          return std::make_shared<Gto>(param.num_warps);
        },
        lv::params("param"), lv::doc("Create a greedy-then-oldest scheduler"));

}  // namespace simtix
