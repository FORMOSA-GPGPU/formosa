/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cores/thread_stack_pool.h"

#include <liblv/output.h>

#include <cassert>

namespace simtix::internal {

void ValidateThreadStackPoolLayout(const ThreadStackPoolLayout &layout,
                                   uint64_t threads_per_core) {
  if (layout.size != 0 && threads_per_core * layout.slot_size == 0) {
    LV_FATAL(
        "Thread stack pool [{:#x}, +{:#x}) needs a nonzero slot size and "
        "thread count, got slot_size={} threads_per_core={}",
        layout.base, layout.size, layout.slot_size, threads_per_core);
  }
}

void CheckThreadStackWrites(const ThreadStackPoolLayout &layout,
                            uint64_t threads_per_core, ExecFlag flag,
                            const sc_bv_base &active_lanes,
                            const std::vector<uint64_t> &addresses,
                            uint32_t wid, uint32_t cwid, uint64_t pc) {
  if (layout.size == 0 || HasException(flag) ||
      !HasFlag(flag, ExecFlag::STORE | ExecFlag::ATOMIC)) {
    return;
  }

  const uint64_t region_size = threads_per_core * layout.slot_size;
  const uint32_t num_lanes = active_lanes.length();
  assert(region_size != 0);
  assert(addresses.size() == num_lanes);
  for (uint32_t lane = 0; lane < num_lanes; ++lane) {
    if (active_lanes[lane] != 1) continue;
    const uint64_t address = addresses[lane];
    if (address < layout.base || address - layout.base >= layout.size) {
      continue;
    }

    // Must match the hart id both cores report through mhartid.
    const uint64_t hart = uint64_t{cwid} * num_lanes + lane;
    const uint64_t offset = address - layout.base;
    const uint64_t slot = (offset % region_size) / layout.slot_size;
    if (slot != hart) {
      LV_FATAL(
          "store/AMO outside the thread's private stack slot: pc={:#x} "
          "wid={} cwid={} lane={} hart={} address={:#x} is in slot {} of "
          "stack region {} (pool [{:#x}, {:#x}), {:#x} bytes per slot)",
          pc, wid, cwid, lane, hart, address, slot, offset / region_size,
          layout.base, layout.base + layout.size, layout.slot_size);
    }
  }
}

}  // namespace simtix::internal
