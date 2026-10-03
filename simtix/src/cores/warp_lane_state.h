// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <systemc.h>

#include <cassert>
#include <cstdint>
#include <vector>

namespace simtix {

// Lane lifetime and barrier participation shared by the SIMT cores.
// Instructions only record arrivals or exits; the core decides when it can
// publish the warp state (including pipeline drain and exception priority).
//
// Exited lanes stay excluded until Reset. Barrier arrivals remain stopped until
// the core receives Resume from WGI and calls ClearBarrier.
class WarpLaneState {
 public:
  WarpLaneState(uint32_t num_warps, uint32_t num_lanes)
      : barrier_(num_warps,
                 sc_dt::sc_bv_base{false, static_cast<int>(num_lanes)}),
        exited_(num_warps,
                sc_dt::sc_bv_base{false, static_cast<int>(num_lanes)}),
        states_(num_warps, State::kRunnable) {}

  void ArriveBarrier(uint32_t wid, const sc_dt::sc_bv_base &lanes) {
    assert(lanes != 0);
    barrier_[wid] |= lanes;
    UpdateState(wid);
  }

  void Exit(uint32_t wid, const sc_dt::sc_bv_base &lanes) {
    assert(lanes != 0);
    exited_[wid] |= lanes;
    UpdateState(wid);
  }

  void ClearBarrier(uint32_t wid) {
    barrier_[wid] = 0;
    UpdateState(wid);
  }

  void Reset(uint32_t wid) {
    barrier_[wid] = 0;
    exited_[wid] = 0;
    states_[wid] = State::kRunnable;
  }

  bool LaneRunnable(uint32_t wid, uint32_t lane) const {
    return !barrier_[wid][lane].to_bool() && !exited_[wid][lane].to_bool();
  }

  bool HasRunnableLanes(uint32_t wid) const {
    return states_[wid] == State::kRunnable;
  }

  bool AllExited(uint32_t wid) const { return states_[wid] == State::kExited; }

  bool BarrierReady(uint32_t wid) const {
    return states_[wid] == State::kBarrier;
  }

 private:
  enum class State : uint8_t { kRunnable, kBarrier, kExited };

  void UpdateState(uint32_t wid) {
    // B = barrier arrivals, E = exited lanes (within this warp's lane width).
    // Runnable lanes: ~(B | E). Warp done: all(E).
    // Barrier ready: any(B) && all(B | E), regardless of which event came last.
    if (exited_[wid].and_reduce()) {
      states_[wid] = State::kExited;
    } else if ((barrier_[wid] | exited_[wid]).and_reduce()) {
      // At least one lane has not exited, so a full union implies a barrier
      // arrival. An all-exited warp must never report a barrier.
      states_[wid] = State::kBarrier;
    } else {
      states_[wid] = State::kRunnable;
    }
  }

  std::vector<sc_dt::sc_bv_base> barrier_;
  std::vector<sc_dt::sc_bv_base> exited_;
  // Cache reductions separately so scheduler scans do not load full masks.
  std::vector<State> states_;
};

}  // namespace simtix
