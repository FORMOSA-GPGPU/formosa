/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <systemc.h>
#include <tlm_core/tlm_1/tlm_req_rsp/tlm_1_interfaces/tlm_core_ifs.h>

#include <queue>

#include "liblv/output.h"

namespace simtix {

// Cycle-accurate delay line between req_q_ and resp_q_.
// An item accepted at cycle N is eligible for output at cycle N + latency.
// Input and output each occur at most once every ticks_per_output cycles.
template <typename T>
class DelayQueue : public sc_module {
 public:
  sc_in<bool> SC_NAMED(clock);
  sc_port<tlm::tlm_get_if<T>> SC_NAMED(req_q_);
  sc_port<tlm::tlm_put_if<T>> SC_NAMED(resp_q_);

  DelayQueue(const sc_module_name &name, uint64_t latency,
             uint64_t ticks_per_output)
      : sc_module(name),
        glb_cnt_(0),
        latency_(latency),
        ticks_per_output_(ticks_per_output),
        in_cnt_(0),
        out_cnt_(0) {
    if (latency_ < 1 || ticks_per_output_ < 1) {
      LV_FATAL(
          "latency and ticks_per_output must be positive. "
          "latency={}, ticks_per_output={}",
          latency_, ticks_per_output_);
    }
    if (latency_ % ticks_per_output_ != 0) {
      LV_FATAL("latency must be a multiple of ticks_per_output.");
    }
    SC_METHOD(Tick);
    sensitive << clock.pos();
  }

 private:
  void Tick() {
    // Output
    if (out_cnt_ == ticks_per_output_ - 1) {
      if (!queue_.empty() && resp_q_->nb_can_put() &&
          glb_cnt_ >= queue_.front().expected_output_time) {
        resp_q_->nb_put(queue_.front().data);
        queue_.pop();
        out_cnt_ = 0;
      }
    } else {
      out_cnt_++;
    }

    // Input
    if (in_cnt_ == ticks_per_output_ - 1) {
      if (req_q_->nb_can_get() &&
          queue_.size() < ((latency_ / ticks_per_output_))) {
        T data;
        req_q_->nb_get(data);
        queue_.push({data, glb_cnt_ + latency_});
        in_cnt_ = 0;
      }
    } else {
      in_cnt_++;
    }

    glb_cnt_++;
  }

  struct QueueItem {
    T data;
    uint64_t expected_output_time;
  };
  std::queue<QueueItem> queue_;
  uint64_t latency_;
  uint64_t glb_cnt_;
  uint64_t ticks_per_output_;
  uint64_t in_cnt_;
  uint64_t out_cnt_;
};

}  // namespace simtix
