/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/schema.h>

#include <cstdint>

namespace simtix {

struct ArchParam {
  uint32_t num_lanes = 4;
  uint32_t num_warps = 4;

  // clang-format off
  LV_SCHEMA_SHARED(simtix, ArchParam,
                   LV_FIELD(num_lanes, "Number of lanes per core"),
                   LV_FIELD(num_warps, "Number of warps per core"));
  // clang-format on
};

}  // namespace simtix
