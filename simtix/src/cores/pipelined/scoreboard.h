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
  kMemHazard,
  kReadBinFull,
};

class Scoreboard {
 public:
  using ChangeCallback = std::function<void(uint32_t wid)>;

  Scoreboard(uint32_t num_warps_per_subcores, uint32_t num_subcores)
      : num_subcores_(num_subcores),
        pc_busy_(num_warps_per_subcores, false),
        mem_busy_(num_warps_per_subcores, false),
        rd_busy_(num_warps_per_subcores * 32, false),
        rs_busy_(num_warps_per_subcores * num_read_bins_, 0) {}

  bool CanIssue(const Packet *packet,
                IssueStallReason *stall_reason = nullptr) const {
    uint32_t local_wid = get_local_wid(packet->wid);

    if (pc_busy_[local_wid]) {
      if (stall_reason) *stall_reason = IssueStallReason::kControlHazard;
      return false;
    }

    if (is_being_written(local_wid, packet->instr.rs1()) ||
        is_being_written(local_wid, packet->instr.rs2()) ||
        is_being_written(local_wid, packet->instr.rs3()) ||
        is_rd_busy(local_wid, packet->instr.rd())) {
      if (stall_reason) *stall_reason = IssueStallReason::kDataHazard;
      return false;
    }

    if (is_mem_busy(local_wid, packet)) {
      if (stall_reason) *stall_reason = IssueStallReason::kMemHazard;
      return false;
    }

    if (is_bin_full(local_wid, packet->instr.rs1()) ||
        is_bin_full(local_wid, packet->instr.rs2()) ||
        is_bin_full(local_wid, packet->instr.rs3())) {
      if (stall_reason) *stall_reason = IssueStallReason::kReadBinFull;
      return false;
    }

    if (stall_reason) *stall_reason = IssueStallReason::kNone;
    return true;
  }

  void Issue(const Packet *packet) {
    update_scoreboard(packet, true);
    update_bloomboard(packet, true);
    if (on_change_) on_change_(packet->wid);
  }

  void RegReadDone(const Packet *packet) {
    update_bloomboard(packet, false);
    if (on_change_) on_change_(packet->wid);
  }

  void Commit(const Packet *packet) {
    update_scoreboard(packet, false);
    if (on_change_) on_change_(packet->wid);
  }

  void set_on_change(ChangeCallback on_change) {
    on_change_ = std::move(on_change);
  }

  uint32_t get_num_read_bins() const { return num_read_bins_; }
  uint32_t get_max_bin_count() const { return max_bin_count_; }

 private:
  inline uint32_t get_local_wid(uint32_t wid) const {
    return wid / num_subcores_;
  }

  inline uint32_t get_bin(uint32_t local_wid, uint32_t rid) const {
    return local_wid * num_read_bins_ + (rid % num_read_bins_);
  }

  inline uint32_t is_bin_full(uint32_t local_wid, uint32_t rid) const {
    uint32_t bin_id = get_bin(local_wid, rid);
    return rid != Instr::kNullReg && rid != 0 &&
           rs_busy_[bin_id] >= max_bin_count_;
  }

  inline bool is_being_written(uint32_t local_wid, uint32_t rid) const {
    return rid != Instr::kNullReg && rid != 0 && rd_busy_[local_wid * 32 + rid];
  }

  inline bool is_being_read(uint32_t local_wid, uint32_t rid) const {
    uint32_t bin_id = get_bin(local_wid, rid);
    return rid != Instr::kNullReg && rid != 0 && rs_busy_[bin_id] > 0;
  }

  inline bool is_rd_busy(uint32_t local_wid, uint32_t rid) const {
    return is_being_written(local_wid, rid) || is_being_read(local_wid, rid);
  }

  inline bool is_rs_busy(uint32_t local_wid, uint32_t rid) const {
    return is_being_written(local_wid, rid) || is_bin_full(local_wid, rid);
  }

  inline bool is_mem_busy(uint32_t local_wid, const Packet *packet) const {
    return packet->instr.is_mem() && mem_busy_[local_wid];
  }

  void update_bloomboard(const Packet *packet, bool allocate) {
    uint32_t local_wid = get_local_wid(packet->wid);
    uint32_t rs1_bin_id = get_bin(local_wid, packet->instr.rs1());
    uint32_t rs2_bin_id = get_bin(local_wid, packet->instr.rs2());
    uint32_t rs3_bin_id = get_bin(local_wid, packet->instr.rs3());

    uint32_t unique_bins[3];
    uint32_t valid_count = 0;

    auto update_rs_bin = [&](uint32_t rs_bin_id) {
      if (allocate) {
        rs_busy_[rs_bin_id]++;
      } else {
        rs_busy_[rs_bin_id]--;
      }
    };

    auto check_unique_bin = [&](uint32_t rid) {
      if (rid != Instr::kNullReg && rid != 0) {
        uint32_t bin_id = get_bin(local_wid, rid);

        for (int i = 0; i < valid_count; ++i) {
          if (unique_bins[i] == bin_id) {
            return;
          }
        }

        unique_bins[valid_count++] = bin_id;
      }
    };

    check_unique_bin(packet->instr.rs1());
    check_unique_bin(packet->instr.rs2());
    check_unique_bin(packet->instr.rs3());

    for (int i = 0; i < valid_count; ++i) {
      update_rs_bin(unique_bins[i]);
    }
  }

  void update_scoreboard(const Packet *packet, bool flag) {
    uint32_t local_wid = get_local_wid(packet->wid);

    if (packet->instr.rd() != Instr::kNullReg && packet->instr.rd() != 0) {
      rd_busy_[local_wid * 32 + packet->instr.rd()] = flag;
    }

    if (packet->instr.is_cti()) {
      pc_busy_[local_wid] = flag;
    }

    if (packet->instr.is_mem()) {
      mem_busy_[local_wid] = flag;
    }
  }

  const uint32_t num_subcores_;
  const uint32_t num_read_bins_ = 8;
  const uint32_t bin_bits_ = 2;
  uint32_t max_bin_count_ = (1 << bin_bits_) - 1;

  std::vector<bool> pc_busy_;
  std::vector<bool> mem_busy_;
  std::vector<bool> rd_busy_;
  std::vector<uint32_t> rs_busy_;
  ChangeCallback on_change_;
};
}  // namespace simtix::pipelined
