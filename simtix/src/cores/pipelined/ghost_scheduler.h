/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/log.h>
#include <liblv/statistics.h>
#include <systemc.h>
#include <tlm_core/tlm_1/tlm_req_rsp/tlm_1_interfaces/tlm_core_ifs.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "cores/pipelined/ghost_param.h"
#include "cores/pipelined/packet.h"
#include "cores/pipelined/param.h"
#include "cores/pipelined/scoreboard.h"
#include "cores/warp_mask.h"
#include "konata/konata.h"

namespace simtix::pipelined {

class ToGhostSchedulerIntf {
 protected:
  virtual ~ToGhostSchedulerIntf() = default;
  virtual const WarpMask &active_warps() const = 0;
  virtual void CaptureThreadMask(Packet *packet) = 0;
  virtual Scoreboard *scoreboard(uint32_t subcore_id) = 0;
  virtual void FreePacket(Packet *packet) = 0;
  virtual konata::KonataTracer<Packet> *tracer() = 0;
  virtual lv::stats::Group *stats_group() = 0;
  friend class GhostScheduler;
};

class GhostScheduler : public sc_module {
 public:
  sc_in<bool> SC_NAMED(clock);
  sc_vector<sc_port<tlm::tlm_get_peek_if<Packet *>>> from_frontend;
  sc_vector<sc_export<tlm::tlm_get_peek_if<Packet *>>> to_backend;

  GhostScheduler(const sc_module_name &name, const ArchParam &p,
                 const Param &pp, const GhostParam &gp, uint32_t subcore_id,
                 ToGhostSchedulerIntf *core);

  // Records that a matching control instruction has finished resolving.
  void ResolveControl(uint32_t wid, uint64_t unique_id);

  // Defers a warp flush until the scheduler performs its next state update.
  void RequestFlush(uint32_t wid);

 private:
  static constexpr uint32_t kInvalidEntry =
      std::numeric_limits<uint32_t>::max();

  static int CheckedIsbEntryCount(uint32_t num_isb_entries) {
    if (num_isb_entries == 0) {
      LV_FATAL("GhOST scheduler IsB entry count is zero");
    }
    if (num_isb_entries >
        static_cast<uint32_t>(std::numeric_limits<int>::max())) {
      LV_FATAL("GhOST scheduler IsB entry count is too large: {}",
               num_isb_entries);
    }
    return static_cast<int>(num_isb_entries);
  }

  static std::size_t CheckedItabEntryCount(uint32_t num_itab_entries) {
    if (num_itab_entries == 0) {
      LV_FATAL("GhOST scheduler ITab entry count is zero");
    }
    return static_cast<std::size_t>(num_itab_entries);
  }

  struct IsbEntry {
    explicit IsbEntry(uint32_t num_isb_entries)
        : isb_dep(false, CheckedIsbEntryCount(num_isb_entries)) {}

    void Reset() {
      valid = false;
      packet = nullptr;
      inst_order = 0;
      isb_dep = 0u;
    }

    bool valid = false;
    Packet *packet = nullptr;
    uint32_t inst_order = 0;
    sc_bv_base isb_dep;

    bool occupied() const { return valid && packet != nullptr; }
  };

  struct ItabEntry {
    bool valid = false;
    uint32_t slot = kInvalidEntry;
  };

  struct WarpState {
    WarpState(uint32_t num_isb_entries, uint32_t num_itab_entries)
        : isb(static_cast<std::size_t>(CheckedIsbEntryCount(num_isb_entries)),
              IsbEntry(num_isb_entries)),
          itab(CheckedItabEntryCount(num_itab_entries)) {}

    std::vector<IsbEntry> isb;
    std::vector<ItabEntry> itab;
    bool control_pending = false;
    uint64_t control_unique_id = 0;
    bool update_pending = false;
    bool scoreboard_changed = false;
    bool flush_pending = false;
  };

  enum class IsbIssueStatus {
    kEmpty,
    kIsbDependency,
    kScoreboardControl,
    kScoreboardData,
    kScoreboardMemory,
    kScoreboardReadBinFull,
    kSerializing,
    kReady,
  };

  enum class DispatchRejectReason : uint32_t {
    kInactive,
    kFlushPending,
    kControlPending,
    kIsbFull,
    kFrontendEmpty,
    kSerializing,
    kCount,
  };

  static constexpr std::size_t kDispatchRejectReasonCount =
      static_cast<std::size_t>(DispatchRejectReason::kCount);

  static constexpr std::size_t DispatchRejectIndex(
      DispatchRejectReason reason) {
    return static_cast<std::size_t>(reason);
  }

  class IssuePort : public tlm::tlm_get_peek_if<Packet *> {
   public:
    IssuePort(GhostScheduler *owner, uint32_t local_wid);
    bool nb_can_get(tlm::tlm_tag<Packet *> *tag = nullptr) const override;
    bool nb_peek(Packet *&packet) const override;
    bool nb_can_peek(tlm::tlm_tag<Packet *> *tag = nullptr) const override;
    Packet *peek(tlm::tlm_tag<Packet *> *tag = nullptr) const override;
    bool nb_get(Packet *&packet) override;
    Packet *get(tlm::tlm_tag<Packet *> *tag = nullptr) override;

    // Provides the event used to notify consumers that a candidate may exist.
    const sc_event &ok_to_get(
        tlm::tlm_tag<Packet *> *tag = nullptr) const override;

    // Provides the event used to notify observers that a candidate may exist.
    const sc_event &ok_to_peek(
        tlm::tlm_tag<Packet *> *tag = nullptr) const override;

    void NotifyCandidateAvailable() { candidate_event_.notify(SC_ZERO_TIME); }

   private:
    GhostScheduler *owner_;
    uint32_t local_wid_;
    sc_event candidate_event_;
  };

  // Advances all GhOST pipeline stages once per positive clock edge.
  void Tick() {
    CollectStateChanges();
    UpdateInstructionTable();
    DispatchToIssueBuffer();
  }

  // Applies deferred changes and records the post-issue scheduler state.
  void CollectStateChanges();

  // Records occupancy and issue-readiness statistics for one active warp.
  void RecordWarpStats(uint32_t local_wid);

  // Moves up to decode_width frontend instructions directly into the IsB.
  void DispatchToIssueBuffer();

  // Selects an eligible local warp and its first available IsB slot.
  std::optional<std::pair<uint32_t, uint32_t>> SelectDispatchTarget(
      std::array<uint32_t, kDispatchRejectReasonCount> &rejected) const;

  // Captures a packet's SIMT mask in program order before out-of-order issue.
  bool CaptureThreadMask(Packet *packet);

  // Detects RAW, WAW, or WAR dependencies between two instructions.
  bool CheckRegisterDependency(const Instr *older, const Instr *newer) const;

  // Applies basic GhOST ordering rules to two memory instruction classes.
  bool CheckMemoryDependency(const Instr *older, const Instr *newer) const;

  // Checks whether a serializing instruction may enter the GhOST pipeline.
  bool CanDispatchSerializing(uint32_t local_wid, const Packet *packet) const;

  // Captures a packet and inserts it into the IsB with its dependencies.
  bool InsertIntoIssueBuffer(Packet *packet, uint32_t local_wid,
                             uint32_t isb_slot);

  // Rebuilds one requested warp's ITab with its oldest ready instructions.
  void UpdateInstructionTable();

  // Selects one warp whose ITab requires an update using round-robin order.
  std::optional<uint32_t> SelectItabUpdateWarp() const;

  // Classifies an IsB entry using the current dependency and scoreboard state.
  IsbIssueStatus EvaluateIsbIssueStatus(const IsbEntry &entry) const;

  // Checks IsB dependencies, scoreboard hazards, and serialization rules.
  bool IsbReady(const IsbEntry &entry) const;

  // Finds the oldest ready IsB slots by program-order index.
  std::optional<uint32_t> SelectOldestReadyIsb(uint32_t local_wid) const;

  // Finds the in-order IsB head used only as an issue-time fallback.
  std::optional<uint32_t> SelectInOrderIsbHead(uint32_t local_wid) const;

  // Counts the occupied IsB entries belonging to one warp.
  uint32_t CountOccupiedIsbEntries(uint32_t local_wid) const;

  // Selects the first valid ITab slot, or the in-order IsB head as fallback.
  std::optional<uint32_t> SelectIssueSlot(uint32_t local_wid) const;

  // Returns the first ITab packet, or the in-order IsB head when ITab is empty.
  bool PeekIssueCandidate(uint32_t local_wid, Packet *&packet) const;

  // Consumes the first ITab packet, or the in-order IsB head when ITab is
  // empty.
  bool GetIssueCandidate(uint32_t local_wid, Packet *&packet);

  // Releases an issued IsB slot, clears dependencies, and compresses indices.
  Packet *ReleaseIssueBufferEntry(uint32_t local_wid, uint32_t slot);

  // Discards all unissued state and packets belonging to one local warp.
  void FlushWarp(uint32_t local_wid);

  // Converts an interleaved global warp ID into this subcore's local warp ID.
  uint32_t get_local_wid(uint32_t wid) const;

  // Converts this subcore's local warp ID into an interleaved global warp ID.
  uint32_t get_wid(uint32_t local_wid) const;

  const uint32_t num_warps_;
  const uint32_t num_local_warps_;
  const uint32_t num_subcores_;
  const uint32_t subcore_id_;
  const uint32_t dispatch_width_;
  const uint32_t num_isb_entries_per_warp_;
  const uint32_t num_itab_entries_per_warp_;

  struct Stats : lv::stats::Group {
    Metric ghost_cycles;
    Metric active_warp_cycles;
    Metric issue_counts;
    Metric ooo_issue_counts;
    Metric issue_order_distance_sum;
    Metric dispatch_cycles;
    Metric dispatch_counts;
    Metric dispatch_select_failure_cycles;
    Metric dispatch_rejected_warp_counts;
    Metric dispatch_rejected_inactive_warps;
    Metric dispatch_rejected_flush_pending_warps;
    Metric dispatch_rejected_control_pending_warps;
    Metric dispatch_rejected_isb_full_warps;
    Metric dispatch_rejected_frontend_empty_warps;
    Metric dispatch_rejected_serializing_warps;
    Metric isb_occupancy_sum;
    Metric isb_empty_warp_cycles;
    Metric isb_full_warp_cycles;
    Metric isb_no_ready_warp_cycles;
    Metric isb_register_or_memory_dep_blocked_warp_cycles;
    Metric isb_scoreboard_blocked_warp_cycles;
    Metric isb_scoreboard_control_blocked_warp_cycles;
    Metric isb_scoreboard_data_blocked_warp_cycles;
    Metric isb_scoreboard_memory_blocked_warp_cycles;
    Metric isb_scoreboard_readbin_blocked_warp_cycles;
    Metric control_pending_warp_cycles;
    Metric itab_update_pending_warp_cycles;

    Formula<Real> issue_rate;
    Formula<Real> ooo_issue_ratio;
    Formula<Real> avg_dispatches_per_ghost_cycle;
    Formula<Real> avg_dispatches_per_dispatch_cycle;
    Formula<Real> dispatch_select_failure_ratio;
    Formula<Real> dispatch_rejected_inactive_ratio;
    Formula<Real> dispatch_rejected_flush_pending_ratio;
    Formula<Real> dispatch_rejected_control_pending_ratio;
    Formula<Real> dispatch_rejected_isb_full_ratio;
    Formula<Real> dispatch_rejected_frontend_empty_ratio;
    Formula<Real> dispatch_rejected_serializing_ratio;
    Formula<Real> avg_issue_order_distance;
    Formula<Real> avg_isb_occupancy;
    Formula<Real> dispatch_cycle_ratio;
    Formula<Real> isb_empty_warp_ratio;
    Formula<Real> isb_full_warp_ratio;
    Formula<Real> isb_no_ready_warp_ratio;
    Formula<Real> isb_register_or_memory_dep_blocked_warp_ratio;
    Formula<Real> isb_scoreboard_blocked_warp_ratio;
    Formula<Real> isb_scoreboard_control_blocked_warp_ratio;
    Formula<Real> isb_scoreboard_data_blocked_warp_ratio;
    Formula<Real> isb_scoreboard_memory_blocked_warp_ratio;
    Formula<Real> isb_scoreboard_readbin_blocked_warp_ratio;
    Formula<Real> control_pending_warp_ratio;
    Formula<Real> itab_update_pending_warp_ratio;

    explicit Stats(const char *name);
  };

  ToGhostSchedulerIntf *const core_;
  Scoreboard *const scoreboard_;
  Stats stats_;

  uint32_t dispatch_prioritized_ = 0;
  uint32_t schedule_prioritized_ = 0;
  std::vector<WarpState> warps_;
  std::vector<std::unique_ptr<IssuePort>> issue_ports_;
};

}  // namespace simtix::pipelined
