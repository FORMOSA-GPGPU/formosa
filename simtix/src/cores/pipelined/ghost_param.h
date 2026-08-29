/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/schema.h>

#include <cstdint>

namespace simtix::pipelined {

class PipelinedCore;

struct GhostParam {
  uint32_t num_isb_entries_per_warp = 8;
  uint32_t num_itab_entries_per_warp = 2;

  LV_SCHEMA(PipelinedCore, GhostParam,
            LV_FIELD(num_isb_entries_per_warp,
                     "Number of issue buffer entries per warp"),
            LV_FIELD(num_itab_entries_per_warp,
                     "Number of instruction table entries per warp"))
};

}  // namespace simtix::pipelined
