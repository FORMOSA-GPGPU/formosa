// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/sched/lrr.h"

#include <liblv/binding.h>

#include "cores/param.h"

namespace simtix {

uint32_t Lrr::SelectWarp(const sc_bv_base &ready_warps) {
  assert(ready_warps != 0);
  for (uint32_t i = 0; i < num_warps_; ++i) {
    uint32_t wid = (prioritized_ + i) % num_warps_;
    if (ready_warps[wid] == 1) {
      prioritized_ = (wid + 1) % num_warps_;
      return wid;
    }
  }
  return -1;
}

LV_BINDING_WITH_BASES(simtix, Lrr, WarpSched)
    .constructor(
        [](const ArchParam &param) {
          return std::make_shared<Lrr>(param.num_warps);
        },
        lv::params("param"), lv::doc("Create a loose round-robin scheduler"));

}  // namespace simtix
