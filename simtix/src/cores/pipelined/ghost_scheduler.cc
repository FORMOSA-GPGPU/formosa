// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/pipelined/ghost_scheduler.h"

#define WITH_TRACER(code)            \
  do {                               \
    if (auto *t = core_->tracer()) { \
      t->code;                       \
    }                                \
  } while (0)

namespace simtix::pipelined {

GhostScheduler::GhostScheduler(const sc_module_name &name,
                               const ArchParam &param, const Param &pipe_param,
                               const GhostParam &ghost_param,
                               uint32_t subcore_id, ToGhostSchedulerIntf *core)
    : sc_module(name),
      from_frontend("from_frontend", param.num_warps / pipe_param.num_subcores),
      to_backend("to_backend", param.num_warps / pipe_param.num_subcores),
      num_warps_(param.num_warps),
      num_local_warps_(param.num_warps / pipe_param.num_subcores),
      num_subcores_(pipe_param.num_subcores),
      subcore_id_(subcore_id),
      dispatch_width_(pipe_param.decode_width),
      num_isb_entries_per_warp_(ghost_param.num_isb_entries_per_warp),
      num_itab_entries_per_warp_(ghost_param.num_itab_entries_per_warp),
      core_(core),
      scoreboard_(core->scoreboard(subcore_id)),
      stats_(name),
      warps_(num_local_warps_,
             WarpState(num_isb_entries_per_warp_, num_itab_entries_per_warp_)) {
  scoreboard_->set_on_change([this](uint32_t wid) {
    if (wid >= num_warps_ || wid % num_subcores_ != subcore_id_) {
      return;
    }
    warps_[get_local_wid(wid)].scoreboard_changed = true;
  });
  core_->stats_group()->add_sub_group(&stats_);
  issue_ports_.reserve(num_local_warps_);
  for (uint32_t local_wid = 0; local_wid < num_local_warps_; ++local_wid) {
    issue_ports_.push_back(std::make_unique<IssuePort>(this, local_wid));
    to_backend[local_wid].bind(*issue_ports_.back());
  }

  SC_METHOD(Tick);
  sensitive << clock.pos();
}

void GhostScheduler::ResolveControl(uint32_t wid, uint64_t unique_id) {
  if (wid >= num_warps_ || wid % num_subcores_ != subcore_id_) {
    return;
  }

  uint32_t local_wid = get_local_wid(wid);
  if (warps_[local_wid].control_pending &&
      warps_[local_wid].control_unique_id == unique_id) {
    warps_[local_wid].control_pending = false;
    warps_[local_wid].control_unique_id = 0;
    warps_[local_wid].update_pending = true;
  }
}

void GhostScheduler::RequestFlush(uint32_t wid) {
  if (wid >= num_warps_ || wid % num_subcores_ != subcore_id_) {
    return;
  }
  warps_[get_local_wid(wid)].flush_pending = true;
}

GhostScheduler::IssuePort::IssuePort(GhostScheduler *owner, uint32_t local_wid)
    : owner_(owner), local_wid_(local_wid) {}

bool GhostScheduler::IssuePort::nb_can_get(tlm::tlm_tag<Packet *> *tag) const {
  Packet *packet = nullptr;
  return owner_->PeekIssueCandidate(local_wid_, packet);
}

bool GhostScheduler::IssuePort::nb_peek(Packet *&packet) const {
  return owner_->PeekIssueCandidate(local_wid_, packet);
}

bool GhostScheduler::IssuePort::nb_can_peek(tlm::tlm_tag<Packet *> *tag) const {
  Packet *packet = nullptr;
  return owner_->PeekIssueCandidate(local_wid_, packet);
}

Packet *GhostScheduler::IssuePort::peek(tlm::tlm_tag<Packet *> *tag) const {
  Packet *packet = nullptr;
  owner_->PeekIssueCandidate(local_wid_, packet);
  return packet;
}

bool GhostScheduler::IssuePort::nb_get(Packet *&packet) {
  return owner_->GetIssueCandidate(local_wid_, packet);
}

Packet *GhostScheduler::IssuePort::get(tlm::tlm_tag<Packet *> *tag) {
  Packet *packet = nullptr;
  owner_->GetIssueCandidate(local_wid_, packet);
  return packet;
}

const sc_event &GhostScheduler::IssuePort::ok_to_get(
    tlm::tlm_tag<Packet *> *tag) const {
  return candidate_event_;
}

const sc_event &GhostScheduler::IssuePort::ok_to_peek(
    tlm::tlm_tag<Packet *> *tag) const {
  return candidate_event_;
}

void GhostScheduler::CollectStateChanges() {
  for (uint32_t local_wid = 0; local_wid < num_local_warps_; ++local_wid) {
    WarpState *warp = &warps_[local_wid];
    if (warp->flush_pending) {
      FlushWarp(local_wid);
      continue;
    }
    if (warp->scoreboard_changed) {
      warp->update_pending = true;
      warp->scoreboard_changed = false;
    }
  }

  bool has_active_warp = false;

  for (uint32_t local_wid = 0; local_wid < num_local_warps_; ++local_wid) {
    const uint32_t wid = get_wid(local_wid);
    if (!core_->active_warps().val()[wid]) {
      continue;
    }

    has_active_warp = true;
    RecordWarpStats(local_wid);
  }

  if (has_active_warp) {
    stats_.ghost_cycles++;
  }
}

void GhostScheduler::RecordWarpStats(uint32_t local_wid) {
  assert(local_wid < num_local_warps_);
  const WarpState &warp = warps_[local_wid];

  uint32_t isb_occupancy = 0;
  bool has_ready = false;
  bool has_isb_dep_blocker = false;
  bool has_scoreboard_blocker = false;
  bool has_scoreboard_control_blocker = false;
  bool has_scoreboard_data_blocker = false;
  bool has_scoreboard_memory_blocker = false;
  bool has_scoreboard_readbin_blocker = false;

  for (const IsbEntry &entry : warp.isb) {
    const IsbIssueStatus status = EvaluateIsbIssueStatus(entry);
    if (status != IsbIssueStatus::kEmpty) {
      ++isb_occupancy;
    }

    switch (status) {
      case IsbIssueStatus::kEmpty:
      case IsbIssueStatus::kSerializing:
        break;
      case IsbIssueStatus::kIsbDependency:
        has_isb_dep_blocker = true;
        break;
      case IsbIssueStatus::kScoreboardControl:
        has_scoreboard_blocker = true;
        has_scoreboard_control_blocker = true;
        break;
      case IsbIssueStatus::kScoreboardData:
        has_scoreboard_blocker = true;
        has_scoreboard_data_blocker = true;
        break;
      case IsbIssueStatus::kScoreboardMemory:
        has_scoreboard_blocker = true;
        has_scoreboard_memory_blocker = true;
        break;
      case IsbIssueStatus::kScoreboardReadBinFull:
        has_scoreboard_blocker = true;
        has_scoreboard_readbin_blocker = true;
        break;
      case IsbIssueStatus::kReady:
        has_ready = true;
        break;
    }
  }

  stats_.active_warp_cycles++;
  stats_.isb_occupancy_sum += isb_occupancy;
  stats_.control_pending_warp_cycles += warp.control_pending;
  stats_.itab_update_pending_warp_cycles += warp.update_pending;

  if (isb_occupancy == num_isb_entries_per_warp_) {
    stats_.isb_full_warp_cycles++;
  }
  if (isb_occupancy == 0) {
    stats_.isb_empty_warp_cycles++;
    return;
  }
  if (has_ready) {
    return;
  }

  stats_.isb_no_ready_warp_cycles++;
  if (has_isb_dep_blocker) {
    stats_.isb_register_or_memory_dep_blocked_warp_cycles++;
  }
  if (!has_scoreboard_blocker) {
    return;
  }

  stats_.isb_scoreboard_blocked_warp_cycles++;
  stats_.isb_scoreboard_control_blocked_warp_cycles +=
      has_scoreboard_control_blocker;
  stats_.isb_scoreboard_data_blocked_warp_cycles += has_scoreboard_data_blocker;
  stats_.isb_scoreboard_memory_blocked_warp_cycles +=
      has_scoreboard_memory_blocker;
  stats_.isb_scoreboard_readbin_blocked_warp_cycles +=
      has_scoreboard_readbin_blocker;
}

void GhostScheduler::DispatchToIssueBuffer() {
  std::optional<uint32_t> first_dispatched_warp;
  bool has_active_warp = false;
  bool has_dispatched = false;

  for (uint32_t local_wid = 0; local_wid < num_local_warps_; ++local_wid) {
    if (core_->active_warps().val()[get_wid(local_wid)]) {
      has_active_warp = true;
      break;
    }
  }

  for (uint32_t dispatch = 0; dispatch < dispatch_width_; ++dispatch) {
    std::array<uint32_t, kDispatchRejectReasonCount> rejected{};
    auto selected = SelectDispatchTarget(rejected);

    if (!selected) {
      // Attribute rejection reasons only to cycles where no slot dispatched a
      // packet. A failure after an earlier dispatch only represents unused
      // residual dispatch width, not a completely blocked cycle.
      if (has_active_warp && !has_dispatched) {
        const auto rejected_count = [&rejected](DispatchRejectReason reason) {
          return rejected[DispatchRejectIndex(reason)];
        };

        stats_.dispatch_select_failure_cycles++;
        stats_.dispatch_rejected_inactive_warps +=
            rejected_count(DispatchRejectReason::kInactive);
        stats_.dispatch_rejected_flush_pending_warps +=
            rejected_count(DispatchRejectReason::kFlushPending);
        stats_.dispatch_rejected_control_pending_warps +=
            rejected_count(DispatchRejectReason::kControlPending);
        stats_.dispatch_rejected_isb_full_warps +=
            rejected_count(DispatchRejectReason::kIsbFull);
        stats_.dispatch_rejected_frontend_empty_warps +=
            rejected_count(DispatchRejectReason::kFrontendEmpty);
        stats_.dispatch_rejected_serializing_warps +=
            rejected_count(DispatchRejectReason::kSerializing);
        stats_.dispatch_rejected_warp_counts += num_local_warps_;
      }
      break;
    }

    const auto [local_wid, isb_slot] = *selected;
    Packet *peeked = nullptr;
    bool peek_succeeded = from_frontend[local_wid]->nb_peek(peeked);
    assert(peek_succeeded);
    assert(peeked != nullptr);
    assert(peeked->wid == get_wid(local_wid));

    Packet *packet = from_frontend[local_wid]->get();
    assert(packet != nullptr);
    assert(packet == peeked);

    if (!InsertIntoIssueBuffer(packet, local_wid, isb_slot)) {
      continue;
    }

    has_dispatched = true;
    stats_.dispatch_counts++;

    if (!first_dispatched_warp) {
      first_dispatched_warp = local_wid;
    }

    // Keep the remaining dispatch width on the same warp. Selection moves to
    // another warp only when this one can no longer dispatch.
    dispatch_prioritized_ = local_wid;
  }

  if (first_dispatched_warp) {
    dispatch_prioritized_ = (*first_dispatched_warp + 1) % num_local_warps_;
  }

  if (has_dispatched) {
    stats_.dispatch_cycles++;
  }
}

std::optional<std::pair<uint32_t, uint32_t>>
GhostScheduler::SelectDispatchTarget(
    std::array<uint32_t, kDispatchRejectReasonCount> &rejected) const {
  const auto reject = [&rejected](DispatchRejectReason reason) {
    rejected[DispatchRejectIndex(reason)]++;
  };

  for (uint32_t i = 0; i < num_local_warps_; ++i) {
    uint32_t local_wid = (dispatch_prioritized_ + i) % num_local_warps_;
    uint32_t wid = get_wid(local_wid);

    if (!core_->active_warps().val()[wid]) {
      reject(DispatchRejectReason::kInactive);
      continue;
    }

    const WarpState *warp = &warps_[local_wid];
    if (warp->flush_pending) {
      reject(DispatchRejectReason::kFlushPending);
      continue;
    }
    if (warp->control_pending) {
      reject(DispatchRejectReason::kControlPending);
      continue;
    }

    std::optional<uint32_t> free_slot;
    for (uint32_t slot = 0; slot < num_isb_entries_per_warp_; ++slot) {
      if (!warp->isb[slot].occupied()) {
        free_slot = slot;
        break;
      }
    }
    if (!free_slot) {
      reject(DispatchRejectReason::kIsbFull);
      continue;
    }

    Packet *packet = nullptr;
    if (!from_frontend[local_wid]->nb_peek(packet) || packet == nullptr) {
      reject(DispatchRejectReason::kFrontendEmpty);
      continue;
    }

    if (!CanDispatchSerializing(local_wid, packet)) {
      reject(DispatchRejectReason::kSerializing);
      continue;
    }

    return std::pair{local_wid, *free_slot};
  }

  return std::nullopt;
}

bool GhostScheduler::CaptureThreadMask(Packet *packet) {
  if (packet == nullptr || packet->wid >= num_warps_ ||
      packet->wid % num_subcores_ != subcore_id_) {
    return false;
  }

  core_->CaptureThreadMask(packet);
  return packet->tmask != 0;
}

bool GhostScheduler::InsertIntoIssueBuffer(Packet *packet, uint32_t local_wid,
                                           uint32_t isb_slot) {
  if (packet == nullptr) {
    return false;
  }

  if (!CaptureThreadMask(packet)) {
    WITH_TRACER(Flush(packet));
    core_->FreePacket(packet);
    return false;
  }

  assert(local_wid < num_local_warps_);
  assert(isb_slot < num_isb_entries_per_warp_);
  WarpState *warp = &warps_[local_wid];
  IsbEntry *inserted = &warp->isb[isb_slot];
  assert(!inserted->occupied());
  const uint32_t inst_order = CountOccupiedIsbEntries(local_wid);
  inserted->valid = true;
  inserted->packet = packet;
  inserted->inst_order = inst_order;
  inserted->isb_dep = 0;

  if (packet->instr.is_control()) {
    warp->control_pending = true;
    warp->control_unique_id = packet->unique_id;
  }

  // Earlier instructions dispatched in this cycle are already in the IsB, so
  // the same dependency pass handles both same-cycle and older instructions.
  for (uint32_t i = 0; i < num_isb_entries_per_warp_; ++i) {
    const IsbEntry &older = warp->isb[i];
    if (i == isb_slot || !older.occupied()) {
      continue;
    }
    inserted->isb_dep[i] =
        CheckRegisterDependency(&older.packet->instr, &packet->instr) ||
        CheckMemoryDependency(&older.packet->instr, &packet->instr);
  }

  WITH_TRACER(StartStage(packet, 0, "GIsB"));
  warp->update_pending = true;
  return true;
}

bool GhostScheduler::CheckRegisterDependency(const Instr *older,
                                             const Instr *newer) const {
  bool has_raw_dependency =
      older->rd() != Instr::kNullReg && older->rd() != 0 &&
      (newer->rs1() == older->rd() || newer->rs2() == older->rd() ||
       newer->rs3() == older->rd());
  bool has_war_or_waw_dependency =
      newer->rd() != Instr::kNullReg && newer->rd() != 0 &&
      (newer->rd() == older->rs1() || newer->rd() == older->rs2() ||
       newer->rd() == older->rs3() || newer->rd() == older->rd());
  return has_raw_dependency || has_war_or_waw_dependency;
}

bool GhostScheduler::CheckMemoryDependency(const Instr *older,
                                           const Instr *newer) const {
  if (!older->is_mem() || !newer->is_mem()) {
    return false;
  }

  if (older->is_load() && newer->is_load()) {
    return false;
  }

  return true;
  // including:
  //   load-store
  //   store-load
  //   store-store
  //   atomic-load/store/atomic
  //   load/store-atomic
}

bool GhostScheduler::CanDispatchSerializing(uint32_t local_wid,
                                            const Packet *packet) const {
  if (!packet->instr.is_serializing()) {
    return true;
  }

  // Serializing instructions wait until all older instructions have issued.
  return CountOccupiedIsbEntries(local_wid) == 0;
}

void GhostScheduler::UpdateInstructionTable() {
  std::optional<uint32_t> selected_warp = SelectItabUpdateWarp();
  if (!selected_warp) {
    return;
  }

  uint32_t local_wid = *selected_warp;
  WarpState *warp = &warps_[local_wid];
  bool has_candidate = false;

  // Clear the old ITab entries before choosing the oldest ready IsB entries.
  for (ItabEntry &itab : warp->itab) {
    itab = ItabEntry{};
  }

  for (uint32_t i = 0; i < num_itab_entries_per_warp_; ++i) {
    auto selected_slot = SelectOldestReadyIsb(local_wid);

    if (!selected_slot) {
      break;
    }

    uint32_t slot = *selected_slot;
    warp->itab[i].valid = true;
    warp->itab[i].slot = slot;
    has_candidate = true;
  }

  warp->update_pending = CountOccupiedIsbEntries(local_wid) != 0;
  schedule_prioritized_ = (local_wid + 1) % num_local_warps_;

  if (has_candidate) {
    issue_ports_[local_wid]->NotifyCandidateAvailable();
  }
}

std::optional<uint32_t> GhostScheduler::SelectItabUpdateWarp() const {
  for (uint32_t i = 0; i < num_local_warps_; ++i) {
    uint32_t local_wid = (schedule_prioritized_ + i) % num_local_warps_;

    if (warps_[local_wid].flush_pending) {
      continue;
    }

    if (warps_[local_wid].update_pending) {
      return local_wid;
    }
  }

  return std::nullopt;
}

GhostScheduler::IsbIssueStatus GhostScheduler::EvaluateIsbIssueStatus(
    const IsbEntry &entry) const {
  if (!entry.occupied()) {
    return IsbIssueStatus::kEmpty;
  }

  if (entry.isb_dep.or_reduce()) {
    return IsbIssueStatus::kIsbDependency;
  }

  IssueStallReason stall_reason = IssueStallReason::kNone;
  if (!scoreboard_->CanIssue(entry.packet, &stall_reason)) {
    switch (stall_reason) {
      case IssueStallReason::kControlHazard:
        return IsbIssueStatus::kScoreboardControl;
      case IssueStallReason::kDataHazard:
        return IsbIssueStatus::kScoreboardData;
      case IssueStallReason::kMemHazard:
        return IsbIssueStatus::kScoreboardMemory;
      case IssueStallReason::kReadBinFull:
        return IsbIssueStatus::kScoreboardReadBinFull;
      case IssueStallReason::kNone:
        assert(false && "Scoreboard rejected issue without a stall reason");
        return IsbIssueStatus::kScoreboardData;
    }
  }

  if (entry.packet->instr.is_serializing() && entry.inst_order != 0) {
    return IsbIssueStatus::kSerializing;
  }

  return IsbIssueStatus::kReady;
}

bool GhostScheduler::IsbReady(const IsbEntry &entry) const {
  return EvaluateIsbIssueStatus(entry) == IsbIssueStatus::kReady;
}

std::optional<uint32_t> GhostScheduler::SelectOldestReadyIsb(
    uint32_t local_wid) const {
  const WarpState &warp = warps_[local_wid];
  std::optional<uint32_t> selected;

  for (uint32_t slot = 0; slot < num_isb_entries_per_warp_; ++slot) {
    const IsbEntry &entry = warp.isb[slot];

    if (!entry.occupied()) {
      continue;
    }

    bool already_in_itab = false;
    for (const ItabEntry &itab : warp.itab) {
      if (itab.valid && itab.slot == slot) {
        already_in_itab = true;
        break;
      }
    }
    if (already_in_itab) {
      continue;
    }

    if (!IsbReady(entry)) {
      continue;
    }

    if (!selected || entry.inst_order < warp.isb[*selected].inst_order) {
      selected = slot;
    }
  }

  return selected;
}

std::optional<uint32_t> GhostScheduler::SelectInOrderIsbHead(
    uint32_t local_wid) const {
  assert(local_wid < num_local_warps_);
  const WarpState &warp = warps_[local_wid];

  for (uint32_t slot = 0; slot < num_isb_entries_per_warp_; ++slot) {
    const IsbEntry &entry = warp.isb[slot];
    if (entry.occupied() && entry.inst_order == 0 &&
        !entry.isb_dep.or_reduce()) {
      return slot;
    }
  }

  return std::nullopt;
}

uint32_t GhostScheduler::CountOccupiedIsbEntries(uint32_t local_wid) const {
  uint32_t count = 0;
  const WarpState *warp = &warps_[local_wid];

  for (const IsbEntry &isb : warp->isb) {
    if (isb.occupied()) {
      ++count;
    }
  }

  return count;
}

std::optional<uint32_t> GhostScheduler::SelectIssueSlot(
    uint32_t local_wid) const {
  assert(local_wid < num_local_warps_);
  const WarpState &warp = warps_[local_wid];

  for (const ItabEntry &itab : warp.itab) {
    if (itab.valid && itab.slot < num_isb_entries_per_warp_ &&
        warp.isb[itab.slot].occupied()) {
      return itab.slot;
    }
  }

  return SelectInOrderIsbHead(local_wid);
}

bool GhostScheduler::PeekIssueCandidate(uint32_t local_wid,
                                        Packet *&packet) const {
  packet = nullptr;
  if (auto slot = SelectIssueSlot(local_wid)) {
    packet = warps_[local_wid].isb[*slot].packet;
    return true;
  }

  return false;
}

bool GhostScheduler::GetIssueCandidate(uint32_t local_wid, Packet *&packet) {
  packet = nullptr;
  if (auto slot = SelectIssueSlot(local_wid)) {
    packet = ReleaseIssueBufferEntry(local_wid, *slot);
    return packet != nullptr;
  }

  return false;
}

Packet *GhostScheduler::ReleaseIssueBufferEntry(uint32_t local_wid,
                                                uint32_t slot) {
  assert(local_wid < num_local_warps_);
  if (slot >= num_isb_entries_per_warp_) {
    return nullptr;
  }

  WarpState *warp = &warps_[local_wid];
  IsbEntry *issued = &warp->isb[slot];
  if (!issued->occupied()) {
    return nullptr;
  }

  Packet *packet = issued->packet;
  const uint32_t issued_inst_order = issued->inst_order;

  stats_.issue_counts++;
  stats_.issue_order_distance_sum += issued_inst_order;
  if (issued_inst_order != 0) {
    stats_.ooo_issue_counts++;
  }

  // compact instruction order and clear dependences of all the other isb
  for (auto &isb : warp->isb) {
    if (!isb.valid || &isb == issued) {
      continue;
    }
    if (isb.inst_order > issued_inst_order) {
      --isb.inst_order;
    }
    isb.isb_dep[slot] = 0;
  }

  // free itab entry
  for (auto &itab : warp->itab) {
    if (itab.valid && (itab.slot == slot)) {
      itab.valid = false;
      itab.slot = kInvalidEntry;
    }
  }

  // free isb entry
  issued->Reset();

  // update warp state
  warp->update_pending = true;

  return packet;
}

void GhostScheduler::FlushWarp(uint32_t local_wid) {
  assert(local_wid < num_local_warps_);

  WarpState *warp = &warps_[local_wid];

  for (uint32_t slot = 0; slot < num_isb_entries_per_warp_; ++slot) {
    IsbEntry *isb = &warp->isb[slot];

    if (isb->occupied()) {
      WITH_TRACER(Flush(isb->packet));
      core_->FreePacket(isb->packet);
    }

    isb->Reset();
  }

  for (ItabEntry &itab : warp->itab) {
    itab = ItabEntry{};
  }

  warp->control_pending = false;
  warp->control_unique_id = 0;
  warp->update_pending = false;
  warp->scoreboard_changed = false;
  warp->flush_pending = false;
}

uint32_t GhostScheduler::get_local_wid(uint32_t wid) const {
  return wid / num_subcores_;
}

uint32_t GhostScheduler::get_wid(uint32_t local_wid) const {
  return local_wid * num_subcores_ + subcore_id_;
}

#undef WITH_TRACER

GhostScheduler::Stats::Stats(const char *name)
    : Group(name),
      LV_STAT(ghost_cycles,
              "Number of GhOST scheduler cycles with active warps"),
      LV_STAT(active_warp_cycles,
              "Accumulated number of active GhOST warps per cycle"),
      LV_STAT(issue_counts, "Number of instructions issued by GhOST"),
      LV_STAT(ooo_issue_counts,
              "Number of out-of-order instructions issued by GhOST"),
      LV_STAT(issue_order_distance_sum,
              "Accumulated IsB order distance of GhOST issues"),
      LV_STAT(dispatch_cycles,
              "Number of active GhOST scheduler cycles that have "
              "dispatched packet"),
      LV_STAT(dispatch_counts,
              "Number of packets dispatched into GhOST issue buffers"),
      LV_STAT(dispatch_select_failure_cycles,
              "Number of active GhOST scheduler cycles with no dispatchable "
              "warp"),
      LV_STAT(dispatch_rejected_warp_counts,
              "Number of warp rejections observed during failed GhOST "
              "dispatch selections"),
      LV_STAT(dispatch_rejected_inactive_warps,
              "Failed-selection warp rejections because the warp is "
              "inactive"),
      LV_STAT(dispatch_rejected_flush_pending_warps,
              "Failed-selection warp rejections because a flush is "
              "pending"),
      LV_STAT(dispatch_rejected_control_pending_warps,
              "Failed-selection warp rejections because control is "
              "pending"),
      LV_STAT(dispatch_rejected_isb_full_warps,
              "Failed-selection warp rejections because the IsB is full"),
      LV_STAT(dispatch_rejected_frontend_empty_warps,
              "Failed-selection warp rejections because the frontend is "
              "empty"),
      LV_STAT(dispatch_rejected_serializing_warps,
              "Failed-selection warp rejections because a serializing "
              "instruction cannot dispatch"),
      LV_STAT(isb_occupancy_sum,
              "Accumulated number of valid GhOST IsB entries"),
      LV_STAT(isb_empty_warp_cycles,
              "Number of active GhOST warp cycles with an empty IsB"),
      LV_STAT(isb_full_warp_cycles,
              "Number of active GhOST warp cycles with a full IsB"),
      LV_STAT(isb_no_ready_warp_cycles,
              "Number of active GhOST warp cycles with a nonempty IsB but "
              "no ready entry"),
      LV_STAT(isb_register_or_memory_dep_blocked_warp_cycles,
              "Number of active GhOST warp cycles with an IsB entry blocked "
              "by an IsB register or memory dependency"),
      LV_STAT(isb_scoreboard_blocked_warp_cycles,
              "Number of active GhOST warp cycles with a dependency-free "
              "IsB entry blocked by the scoreboard"),
      LV_STAT(isb_scoreboard_control_blocked_warp_cycles,
              "Number of no-ready GhOST IsB warp cycles with a scoreboard "
              "control-hazard blocker"),
      LV_STAT(isb_scoreboard_data_blocked_warp_cycles,
              "Number of no-ready GhOST IsB warp cycles with a scoreboard "
              "data-hazard blocker"),
      LV_STAT(isb_scoreboard_memory_blocked_warp_cycles,
              "Number of no-ready GhOST IsB warp cycles with a scoreboard "
              "memory-hazard blocker"),
      LV_STAT(isb_scoreboard_readbin_blocked_warp_cycles,
              "Number of no-ready GhOST IsB warp cycles with a scoreboard "
              "read-bin-full blocker"),
      LV_STAT(control_pending_warp_cycles,
              "Number of active GhOST warp cycles with unresolved control"),
      LV_STAT(itab_update_pending_warp_cycles,
              "Number of active GhOST warp cycles awaiting an ITab update"),
      LV_STAT(issue_rate, "GhOST issues per active GhOST scheduler cycle"),
      LV_STAT(ooo_issue_ratio,
              "Fraction of GhOST issues that are out of order"),
      LV_STAT(avg_dispatches_per_ghost_cycle,
              "Average packets dispatched per GhOST scheduler cycle"),
      LV_STAT(avg_dispatches_per_dispatch_cycle,
              "Average packets dispatched per dispatch cycle"),
      LV_STAT(dispatch_select_failure_ratio,
              "Fraction of active GhOST scheduler cycles with no "
              "dispatchable warp"),
      LV_STAT(dispatch_rejected_inactive_ratio,
              "Fraction of failed-selection warp rejections that are "
              "inactive"),
      LV_STAT(dispatch_rejected_flush_pending_ratio,
              "Fraction of failed-selection warp rejections with a "
              "pending flush"),
      LV_STAT(dispatch_rejected_control_pending_ratio,
              "Fraction of failed-selection warp rejections with pending "
              "control"),
      LV_STAT(dispatch_rejected_isb_full_ratio,
              "Fraction of failed-selection warp rejections with a full "
              "IsB"),
      LV_STAT(dispatch_rejected_frontend_empty_ratio,
              "Fraction of failed-selection warp rejections with an empty "
              "frontend"),
      LV_STAT(dispatch_rejected_serializing_ratio,
              "Fraction of failed-selection warp rejections blocked by "
              "serialization"),
      LV_STAT(avg_issue_order_distance,
              "Average IsB order distance of out-of-order GhOST issues"),
      LV_STAT(avg_isb_occupancy,
              "Average number of valid IsB entries per active warp"),
      LV_STAT(dispatch_cycle_ratio,
              "Fraction of active GhOST scheduler cycles that have "
              "dispatched packet"),
      LV_STAT(isb_empty_warp_ratio,
              "Fraction of active GhOST warp cycles with an empty IsB"),
      LV_STAT(isb_full_warp_ratio,
              "Fraction of active GhOST warp cycles with a full IsB"),
      LV_STAT(isb_no_ready_warp_ratio,
              "Fraction of active GhOST warp cycles with a nonempty IsB but "
              "no ready entry"),
      LV_STAT(isb_register_or_memory_dep_blocked_warp_ratio,
              "Fraction of no-ready GhOST IsB warp cycles containing a "
              "register or memory dependency blocker"),
      LV_STAT(isb_scoreboard_blocked_warp_ratio,
              "Fraction of no-ready GhOST IsB warp cycles containing a "
              "scoreboard blocker"),
      LV_STAT(isb_scoreboard_control_blocked_warp_ratio,
              "Fraction of scoreboard-blocked GhOST IsB warp cycles with a "
              "control hazard"),
      LV_STAT(isb_scoreboard_data_blocked_warp_ratio,
              "Fraction of scoreboard-blocked GhOST IsB warp cycles with a "
              "data hazard"),
      LV_STAT(isb_scoreboard_memory_blocked_warp_ratio,
              "Fraction of scoreboard-blocked GhOST IsB warp cycles with a "
              "memory hazard"),
      LV_STAT(isb_scoreboard_readbin_blocked_warp_ratio,
              "Fraction of scoreboard-blocked GhOST IsB warp cycles with a "
              "full read bin"),
      LV_STAT(control_pending_warp_ratio,
              "Fraction of active GhOST warp cycles with unresolved control"),
      LV_STAT(itab_update_pending_warp_ratio,
              "Fraction of active GhOST warp cycles awaiting an ITab update") {
  issue_rate = issue_counts / ghost_cycles;
  ooo_issue_ratio = ooo_issue_counts / issue_counts;
  avg_dispatches_per_ghost_cycle = dispatch_counts / ghost_cycles;
  avg_dispatches_per_dispatch_cycle = dispatch_counts / dispatch_cycles;
  dispatch_select_failure_ratio = dispatch_select_failure_cycles / ghost_cycles;
  dispatch_rejected_inactive_ratio =
      dispatch_rejected_inactive_warps / dispatch_rejected_warp_counts;
  dispatch_rejected_flush_pending_ratio =
      dispatch_rejected_flush_pending_warps / dispatch_rejected_warp_counts;
  dispatch_rejected_control_pending_ratio =
      dispatch_rejected_control_pending_warps / dispatch_rejected_warp_counts;
  dispatch_rejected_isb_full_ratio =
      dispatch_rejected_isb_full_warps / dispatch_rejected_warp_counts;
  dispatch_rejected_frontend_empty_ratio =
      dispatch_rejected_frontend_empty_warps / dispatch_rejected_warp_counts;
  dispatch_rejected_serializing_ratio =
      dispatch_rejected_serializing_warps / dispatch_rejected_warp_counts;
  avg_issue_order_distance = issue_order_distance_sum / ooo_issue_counts;
  avg_isb_occupancy = isb_occupancy_sum / active_warp_cycles;
  dispatch_cycle_ratio = dispatch_cycles / ghost_cycles;
  isb_empty_warp_ratio = isb_empty_warp_cycles / active_warp_cycles;
  isb_full_warp_ratio = isb_full_warp_cycles / active_warp_cycles;
  isb_no_ready_warp_ratio = isb_no_ready_warp_cycles / active_warp_cycles;
  isb_register_or_memory_dep_blocked_warp_ratio =
      isb_register_or_memory_dep_blocked_warp_cycles / isb_no_ready_warp_cycles;
  isb_scoreboard_blocked_warp_ratio =
      isb_scoreboard_blocked_warp_cycles / isb_no_ready_warp_cycles;
  isb_scoreboard_control_blocked_warp_ratio =
      isb_scoreboard_control_blocked_warp_cycles /
      isb_scoreboard_blocked_warp_cycles;
  isb_scoreboard_data_blocked_warp_ratio =
      isb_scoreboard_data_blocked_warp_cycles /
      isb_scoreboard_blocked_warp_cycles;
  isb_scoreboard_memory_blocked_warp_ratio =
      isb_scoreboard_memory_blocked_warp_cycles /
      isb_scoreboard_blocked_warp_cycles;
  isb_scoreboard_readbin_blocked_warp_ratio =
      isb_scoreboard_readbin_blocked_warp_cycles /
      isb_scoreboard_blocked_warp_cycles;
  control_pending_warp_ratio = control_pending_warp_cycles / active_warp_cycles;
  itab_update_pending_warp_ratio =
      itab_update_pending_warp_cycles / active_warp_cycles;
}

}  // namespace simtix::pipelined
