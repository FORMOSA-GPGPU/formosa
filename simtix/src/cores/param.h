/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/schema.h>

#include <cstdint>

namespace simtix {

struct ThreadStackPoolLayout {
  uint64_t base = 0;
  uint64_t size = 0;
  uint64_t slot_size = 0;

  // clang-format off
  LV_SCHEMA_SHARED(simtix, ThreadStackPoolLayout,
                   LV_FIELD(base, "Start address of the thread stack pool"),
                   LV_FIELD(size, "Size of the thread stack pool"),
                   LV_FIELD(slot_size, "Size of one thread's stack slot"));
  // clang-format on
};

struct ArchParam {
  uint32_t num_lanes = 4;
  uint32_t num_warps = 4;
  ThreadStackPoolLayout thread_stack_pool;

  // clang-format off
  LV_SCHEMA_SHARED(simtix, ArchParam,
                   LV_FIELD(num_lanes, "Number of lanes per core"),
                   LV_FIELD(num_warps, "Number of warps per core"),
                   LV_FIELD(thread_stack_pool, "Stack pool of the core's threads"));
  // clang-format on
};

}  // namespace simtix
