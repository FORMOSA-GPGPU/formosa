/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <systemc.h>

#include <cstdint>
#include <vector>

#include "cores/exec_flag.h"
#include "cores/param.h"

namespace simtix::internal {

// Validates an enabled pool's slot size and thread count at initialization.
// Terminates the simulation if they produce a zero-sized work-group region.
void ValidateThreadStackPoolLayout(const ThreadStackPoolLayout &layout,
                                   uint64_t threads_per_core);

// Stops the simulation if an active store/AMO writes another hart's slot.
// This is a simulator-only diagnostic; FORMOSA hardware has no such check.
// Exceptional instructions and zero-sized pools are ignored. Loads are not
// checked, since workloads may read just past a global buffer into the pool.
// Region ownership and out-of-pool addresses are not checked.
//
// The layout must have been validated with the same threads_per_core before
// use. The mask and address vector must both contain num_lanes entries, where
// num_lanes is the core's lane count. Arguments are only read during the call.
void CheckThreadStackWrites(const ThreadStackPoolLayout &layout,
                            uint64_t threads_per_core, ExecFlag flag,
                            const sc_bv_base &active_lanes,
                            const std::vector<uint64_t> &addresses,
                            uint32_t wid, uint32_t cwid, uint64_t pc);

}  // namespace simtix::internal
