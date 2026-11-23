/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <systemc.h>

#include <functional>

#include "cores/pipelined/packet.h"

namespace simtix::pipelined {

enum class IssueStallReason {
  kNone,
  kControlHazard,
  kDataHazard,
};

class Scoreboard {
 public:
  using ChangeCallback = std::function<void(uint32_t wid)>;

  Scoreboard(uint32_t num_warps_per_subcores, uint32_t num_subcores)
      : num_subcores_(num_subcores),
        control_busy_(num_warps_per_subcores, false),
        rd_busy_(num_warps_per_subcores * 32, false) {}

  bool CanIssue(const Packet *packet,
                IssueStallReason *stall_reason = nullptr) const {
    uint32_t local_wid = get_local_wid(packet->wid);

    if (control_busy_[local_wid]) {
      if (stall_reason) *stall_reason = IssueStallReason::kControlHazard;
      return false;
    }

    if (is_being_written(local_wid, packet->instr.rs1()) ||
        is_being_written(local_wid, packet->instr.rs2()) ||
        is_being_written(local_wid, packet->instr.rs3()) ||
        is_being_written(local_wid, packet->instr.rd())) {
      if (stall_reason) *stall_reason = IssueStallReason::kDataHazard;
      return false;
    }

    if (stall_reason) *stall_reason = IssueStallReason::kNone;
    return true;
  }

  void Issue(const Packet *packet) {
    update_scoreboard(packet, true);
    if (on_change_) on_change_(packet->wid);
  }

  void Commit(const Packet *packet) {
    update_scoreboard(packet, false);
    if (on_change_) on_change_(packet->wid);
  }

  void set_on_change(ChangeCallback on_change) {
    on_change_ = std::move(on_change);
  }

 private:
  inline uint32_t get_local_wid(uint32_t wid) const {
    return wid / num_subcores_;
  }

  inline bool is_being_written(uint32_t local_wid, uint32_t rid) const {
    return rid != Instr::kNullReg && rid != 0 && rd_busy_[local_wid * 32 + rid];
  }

  void update_scoreboard(const Packet *packet, bool flag) {
    uint32_t local_wid = get_local_wid(packet->wid);

    if (packet->instr.rd() != Instr::kNullReg && packet->instr.rd() != 0) {
      rd_busy_[local_wid * 32 + packet->instr.rd()] = flag;
    }

    // Both control transfers and scheduling operations must resolve before the
    // warp can issue another instruction.
    if (packet->instr.is_cti() || packet->instr.affects_scheduling()) {
      control_busy_[local_wid] = flag;
    }
  }

  const uint32_t num_subcores_;

  std::vector<bool> control_busy_;
  std::vector<bool> rd_busy_;
  ChangeCallback on_change_;
};
}  // namespace simtix::pipelined
