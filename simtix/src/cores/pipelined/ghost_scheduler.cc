// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/pipelined/ghost_scheduler.h"

#include <fmt/format.h>
#include <fmt/ostream.h>

#include <algorithm>

#include "cores/pipelined/ghost_param.h"

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
      num_lanes_(param.num_lanes),
      num_subcores_(pipe_param.num_subcores),
      subcore_id_(subcore_id),
      num_dcs_(pipe_param.decode_width),
      num_isb_entries_per_warp_(ghost_param.num_isb_entries_per_warp),
      num_itab_entries_per_warp_(ghost_param.num_itab_entries_per_warp),
      core_(core),
      scoreboard_(core->scoreboard(subcore_id)),
      stats_(name),
      dispatched_this_cycle_(num_local_warps_, false),
      dcs_(num_dcs_, DependenceChecker(num_isb_entries_per_warp_)),
      warps_(num_local_warps_,
             WarpState(num_isb_entries_per_warp_, num_itab_entries_per_warp_)) {
  scoreboard_->set_on_change([this](uint32_t wid) {
    if (wid >= num_warps_ || wid % num_subcores_ != subcore_id_) {
      return;
    }
    warps_[get_local_wid(wid)].scoreboard_changed = true;
  });
  core_->stats_group()->add_sub_group(&stats_);
  for (WarpState &warp : warps_) {
    for (uint32_t slot = 0; slot < num_isb_entries_per_warp_; ++slot) {
      warp.free_isb_idx_q.push(slot);
    }
  }
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
    stats_.active_warp_cycles++;
    const WarpState &warp = warps_[local_wid];
    const uint32_t isb_occupancy = CountValidIsbEntries(local_wid);
    stats_.isb_occupancy_sum += isb_occupancy;

    if (warp.control_pending) {
      stats_.control_pending_warp_cycles++;
    }
    if (warp.update_pending) {
      stats_.itab_update_pending_warp_cycles++;
    }
    if (isb_occupancy == num_isb_entries_per_warp_) {
      stats_.isb_full_warp_cycles++;
    }
    if (isb_occupancy == 0) {
      stats_.isb_empty_warp_cycles++;
      continue;
    }

    bool has_ready = false;
    bool has_isb_dep_blocker = false;
    bool has_scoreboard_blocker = false;
    bool has_scoreboard_control_blocker = false;
    bool has_scoreboard_data_blocker = false;
    bool has_scoreboard_memory_blocker = false;
    bool has_scoreboard_readbin_blocker = false;

    for (uint32_t slot = 0; slot < num_isb_entries_per_warp_; ++slot) {
      const IsbEntry &isb = warp.isb[slot];
      if (!isb.occupied()) {
        continue;
      }

      if (isb.isb_dep.or_reduce()) {
        has_isb_dep_blocker = true;
        continue;
      }
      IssueStallReason stall_reason = IssueStallReason::kNone;
      if (!scoreboard_->CanIssue(isb.packet, &stall_reason)) {
        has_scoreboard_blocker = true;
        switch (stall_reason) {
          case IssueStallReason::kControlHazard:
            has_scoreboard_control_blocker = true;
            break;
          case IssueStallReason::kDataHazard:
            has_scoreboard_data_blocker = true;
            break;
          case IssueStallReason::kMemHazard:
            has_scoreboard_memory_blocker = true;
            break;
          case IssueStallReason::kReadBinFull:
            has_scoreboard_readbin_blocker = true;
            break;
          case IssueStallReason::kNone:
            break;
        }
        continue;
      }
      if (isb.packet->instr.is_serializing() && isb.inst_order != 0) {
        continue;
      }
      has_ready = true;
    }

    if (!has_ready) {
      stats_.isb_no_ready_warp_cycles++;
      if (has_isb_dep_blocker) {
        stats_.isb_register_or_memory_dep_blocked_warp_cycles++;
      }
      if (has_scoreboard_blocker) {
        stats_.isb_scoreboard_blocked_warp_cycles++;
        if (has_scoreboard_control_blocker) {
          stats_.isb_scoreboard_control_blocked_warp_cycles++;
        }
        if (has_scoreboard_data_blocker) {
          stats_.isb_scoreboard_data_blocked_warp_cycles++;
        }
        if (has_scoreboard_memory_blocker) {
          stats_.isb_scoreboard_memory_blocked_warp_cycles++;
        }
        if (has_scoreboard_readbin_blocker) {
          stats_.isb_scoreboard_readbin_blocked_warp_cycles++;
        }
      }
    }
  }

  if (has_active_warp) {
    stats_.ghost_cycles++;
  }
}

void GhostScheduler::DispatchToDependenceCheckers() {
  std::fill(dispatched_this_cycle_.begin(), dispatched_this_cycle_.end(),
            false);
  std::optional<uint32_t> first_dispatched_warp;
  bool has_active_warp = false;
  bool has_available_dc = false;
  bool has_dispatched = false;

  for (uint32_t local_wid = 0; local_wid < num_local_warps_; ++local_wid) {
    if (core_->active_warps().val()[get_wid(local_wid)]) {
      has_active_warp = true;
      break;
    }
  }

  for (auto &dc : dcs_) {
    if (dc.valid) {
      // dc is unavailable
      continue;
    }

    has_available_dc = true;
    DispatchSelection selection = SelectDispatchWarp();
    if (!selection.warp) {
      // Attribute rejection reasons only to cycles where no DC dispatched a
      // packet.  A failed selection after an earlier DC succeeded is merely
      // unused residual dispatch width, not a completely blocked cycle.
      if (has_active_warp && !has_dispatched) {
        const auto rejected = [&selection](DispatchRejectReason reason) {
          return selection.rejected[DispatchRejectIndex(reason)];
        };

        stats_.dispatch_select_failure_cycles++;
        stats_.dispatch_rejected_already_dispatched_warps +=
            rejected(DispatchRejectReason::kAlreadyDispatched);
        stats_.dispatch_rejected_inactive_warps +=
            rejected(DispatchRejectReason::kInactive);
        stats_.dispatch_rejected_flush_pending_warps +=
            rejected(DispatchRejectReason::kFlushPending);
        stats_.dispatch_rejected_control_pending_warps +=
            rejected(DispatchRejectReason::kControlPending);
        stats_.dispatch_rejected_isb_full_warps +=
            rejected(DispatchRejectReason::kIsbFull);
        stats_.dispatch_rejected_frontend_empty_warps +=
            rejected(DispatchRejectReason::kFrontendEmpty);
        stats_.dispatch_rejected_serializing_warps +=
            rejected(DispatchRejectReason::kSerializing);
        stats_.dispatch_rejected_warp_counts += num_local_warps_;
      }
      break;
    }

    uint32_t local_wid = *selection.warp;
    Packet *peeked = nullptr;
    bool peek_succeeded = from_frontend[local_wid]->nb_peek(peeked);
    assert(peek_succeeded);
    assert(peeked != nullptr);
    assert(peeked->wid == get_wid(local_wid));

    Packet *packet = from_frontend[local_wid]->get();
    assert(packet != nullptr);
    assert(packet == peeked);

    WITH_TRACER(StartStage(packet, 0, "GDC"));

    if (!InitializeDependenceChecker(dc, packet, local_wid)) {
      continue;
    }

    dispatched_this_cycle_[local_wid] = true;
    has_dispatched = true;
    stats_.dc_dispatch_total_counts++;

    if (!first_dispatched_warp) {
      first_dispatched_warp = local_wid;
    }
  }

  if (first_dispatched_warp) {
    dispatch_prioritized_ = (*first_dispatched_warp + 1) % num_local_warps_;
  }

  if (has_dispatched) {
    stats_.dc_has_dispatch_cycles++;
  }

  if (has_active_warp && !has_available_dc) {
    stats_.dc_not_available_cycles++;
  }
}

GhostScheduler::DispatchSelection GhostScheduler::SelectDispatchWarp() const {
  DispatchSelection selection;

  const auto reject = [&selection](DispatchRejectReason reason) {
    selection.rejected[DispatchRejectIndex(reason)]++;
  };

  for (uint32_t i = 0; i < num_local_warps_; ++i) {
    uint32_t local_wid = (dispatch_prioritized_ + i) % num_local_warps_;
    uint32_t wid = get_wid(local_wid);

    if (dispatched_this_cycle_[local_wid]) {
      reject(DispatchRejectReason::kAlreadyDispatched);
      continue;
    }

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

    // IsB potentially full
    if (CountValidIsbEntries(local_wid) +
            CountOccupiedDependenceCheckers(local_wid) >=
        num_isb_entries_per_warp_) {
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

    selection.warp = local_wid;
    return selection;
  }

  return selection;
}

bool GhostScheduler::CaptureThreadMask(Packet *packet) {
  if (packet == nullptr || packet->wid >= num_warps_ ||
      packet->wid % num_subcores_ != subcore_id_) {
    return false;
  }

  core_->CaptureThreadMask(packet);
  return packet->tmask != 0;
}

bool GhostScheduler::InitializeDependenceChecker(DependenceChecker &dc,
                                                 Packet *packet,
                                                 uint32_t local_wid) {
  if (packet == nullptr) {
    return false;
  }

  if (!CaptureThreadMask(packet)) {
    WITH_TRACER(Flush(packet));
    core_->FreePacket(packet);
    return false;
  }

  WarpState *warp = &warps_[local_wid];

  dc.valid = true;
  dc.packet = packet;
  dc.local_wid = local_wid;
  dc.inst_order = warp->next_inst_order;
  dc.isb_dep = 0;
  dc.compare_mask = 0;

  warp->next_inst_order++;

  if (packet->instr.is_control()) {
    warp->control_pending = true;
    warp->control_unique_id = packet->unique_id;
  }

  // get IsB compare mask
  for (uint32_t i = 0; i < num_isb_entries_per_warp_; ++i) {
    IsbEntry *isb = &warp->isb[i];
    if (!isb->occupied()) {
      continue;
    }
    dc.compare_mask[i] = 1;
    dc.isb_dep[i] =
        CheckRegisterDependency(&isb->packet->instr, &packet->instr) ||
        CheckMemoryDependency(&isb->packet->instr, &packet->instr);
  }

  return true;
}

void GhostScheduler::AdvanceDependenceCheckers() {
  for (uint32_t dc_id = 0; dc_id < num_dcs_; ++dc_id) {
    DependenceChecker *dc = &dcs_[dc_id];
    if (!dc->valid) {
      continue;
    }

    // update isb-dep
    for (uint32_t i = 0; i < num_isb_entries_per_warp_; ++i) {
      if (!dc->compare_mask[i]) {
        continue;
      }

      IsbEntry *isb = &warps_[dc->local_wid].isb[i];
      if (!isb->occupied()) {
        dc->isb_dep[i] = 0;
        dc->compare_mask[i] = 0;
        continue;
      }
    }

    if (InsertIssueBuffer(dc_id)) {
      // clear DC after inserting into IsB
      dc->Reset();
    }
  }
}

bool GhostScheduler::CheckRegisterDependency(const Instr *older,
                                             const Instr *newer) const {
  bool rs_busy = older->rd() != Instr::kNullReg && older->rd() != 0 &&
                 (newer->rs1() == older->rd() || newer->rs2() == older->rd() ||
                  newer->rs3() == older->rd());
  bool rd_busy = newer->rd() != Instr::kNullReg && newer->rd() != 0 &&
                 (newer->rd() == older->rs1() || newer->rd() == older->rs2() ||
                  newer->rd() == older->rs3() || newer->rd() == older->rd());
  return rs_busy || rd_busy;
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

  // serializing instructions must wait DC and IsB become empty
  // to prevent out of order
  return CountValidIsbEntries(local_wid) == 0 &&
         CountOccupiedDependenceCheckers(local_wid) == 0;
}

bool GhostScheduler::InsertIssueBuffer(uint32_t dc_id) {
  if (dc_id >= num_dcs_) {
    return false;
  }

  DependenceChecker *dc = &dcs_[dc_id];
  if (!dc->valid || dc->packet == nullptr) {
    return false;
  }

  WarpState *warp = &warps_[dc->local_wid];

  if (warp->free_isb_idx_q.empty()) {
    return false;
  }

  uint32_t slot = warp->free_isb_idx_q.front();
  warp->free_isb_idx_q.pop();

  // move packet from DC to IsB
  IsbEntry *isb = &warp->isb[slot];
  isb->valid = true;
  isb->packet = dc->packet;
  isb->inst_order = dc->inst_order;
  isb->isb_dep = dc->isb_dep;

  WITH_TRACER(StartStage(isb->packet, 0, "GIsB"));

  // clear DC
  dc->Reset();
  warp->update_pending = true;
  return true;
}

void GhostScheduler::UpdateInstructionTable() {
  std::optional<uint32_t> selected_warp = SelectScheduleWarp();
  if (!selected_warp) {
    return;
  }

  uint32_t local_wid = *selected_warp;
  WarpState *warp = &warps_[local_wid];
  bool has_candidate = false;

  for (ItabEntry &itab : warp->itab) {
    itab = ItabEntry{};
  }

  for (uint32_t i = 0; i < num_itab_entries_per_warp_; ++i) {
    auto selected_slot = SelectOldestReadyIsb(local_wid);
    bool selected_oldest_fallback = false;

    // put the oldest IsB entry into ITab if no ready isb is selected
    // (only wait for scoreboard dependency)
    if (!selected_slot && i == 0) {
      for (uint32_t slot = 0; slot < num_isb_entries_per_warp_; ++slot) {
        const IsbEntry &entry = warp->isb[slot];
        if (!entry.occupied() || entry.inst_order != 0) {
          continue;
        }
        selected_slot = slot;
        selected_oldest_fallback = true;
        break;
      }
    }

    if (!selected_slot) {
      break;
    }

    uint32_t slot = *selected_slot;
    warp->itab[i].valid = true;
    warp->itab[i].slot = slot;
    has_candidate = true;

    Packet *packet = warp->isb[slot].packet;
  }

  warp->update_pending = CountValidIsbEntries(local_wid) != 0;
  schedule_prioritized_ = (local_wid + 1) % num_local_warps_;

  if (has_candidate) {
    issue_ports_[local_wid]->NotifyCandidateAvailable();
  } else {
    stats_.itab_no_ready_isb_selected_cycles++;
  }
}

std::optional<uint32_t> GhostScheduler::SelectScheduleWarp() const {
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

bool GhostScheduler::IsbReady(uint32_t local_wid, uint32_t slot) const {
  assert(local_wid < num_local_warps_);
  if (slot >= num_isb_entries_per_warp_) {
    return false;
  }

  const WarpState *warp = &warps_[local_wid];

  const IsbEntry *isb = &warp->isb[slot];
  if (!isb->occupied()) {
    return false;
  }

  // check dependency among IsB
  if (isb->isb_dep.or_reduce()) {
    return false;
  }

  // check dependency with scoreboard
  if (!scoreboard_->CanIssue(isb->packet)) {
    return false;
  }

  if (isb->packet->instr.is_serializing() && isb->inst_order != 0) {
    return false;
  }

  return true;
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

    if (!IsbReady(local_wid, slot)) {
      continue;
    }

    if (!selected || entry.inst_order < warp.isb[*selected].inst_order) {
      selected = slot;
    }
  }

  return selected;
}

uint32_t GhostScheduler::CountValidIsbEntries(uint32_t local_wid) const {
  uint32_t count = 0;
  const WarpState *warp = &warps_[local_wid];

  for (const IsbEntry &isb : warp->isb) {
    if (isb.occupied()) {
      ++count;
    }
  }

  return count;
}

uint32_t GhostScheduler::CountOccupiedDependenceCheckers(
    uint32_t local_wid) const {
  uint32_t count = 0;

  for (const DependenceChecker &dc : dcs_) {
    if (dc.valid && dc.local_wid == local_wid) {
      ++count;
    }
  }

  return count;
}

bool GhostScheduler::PeekIssueCandidate(uint32_t local_wid,
                                        Packet *&packet) const {
  packet = nullptr;
  assert(local_wid < num_local_warps_);

  const WarpState &warp = warps_[local_wid];

  for (const auto &itab : warp.itab) {
    if (!itab.valid || itab.slot >= num_isb_entries_per_warp_) {
      continue;
    }

    const auto &isb = warp.isb[itab.slot];
    if (!isb.occupied()) {
      continue;
    }

    packet = isb.packet;
    return true;
  }

  return false;
}

bool GhostScheduler::GetIssueCandidate(uint32_t local_wid, Packet *&packet) {
  packet = nullptr;

  assert(local_wid < num_local_warps_);

  const WarpState &warp = warps_[local_wid];

  for (const auto &itab : warp.itab) {
    if (!itab.valid || itab.slot >= num_isb_entries_per_warp_) {
      continue;
    }

    const auto &isb = warp.isb[itab.slot];
    if (!isb.occupied()) {
      continue;
    }

    packet = ReleaseIssueBufferEntry(local_wid, itab.slot);
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
  Packet *packet = issued->packet;
  uint32_t issued_inst_order = issued->inst_order;

  if (!issued->occupied()) {
    return nullptr;
  }

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

  // compact instruction order in dependence checker and clear dependence of
  // that slot
  for (auto &dc : dcs_) {
    if (!dc.valid || dc.local_wid != local_wid) {
      continue;
    }
    if (dc.inst_order > issued_inst_order) {
      --dc.inst_order;
    }
    dc.compare_mask[slot] = 0;
    dc.isb_dep[slot] = 0;
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
  assert(warp->next_inst_order > 0);
  warp->next_inst_order--;
  warp->free_isb_idx_q.push(slot);
  warp->update_pending = true;

  return packet;
}

void GhostScheduler::FlushWarp(uint32_t local_wid) {
  assert(local_wid < num_local_warps_);

  WarpState *warp = &warps_[local_wid];

  for (DependenceChecker &dc : dcs_) {
    if (!dc.valid || dc.local_wid != local_wid) {
      continue;
    }
    if (dc.packet != nullptr) {
      WITH_TRACER(Flush(dc.packet));
      core_->FreePacket(dc.packet);
    }

    dc.Reset();
  }

  while (!warp->free_isb_idx_q.empty()) {
    warp->free_isb_idx_q.pop();
  }

  for (uint32_t slot = 0; slot < num_isb_entries_per_warp_; ++slot) {
    IsbEntry *isb = &warp->isb[slot];

    if (isb->occupied()) {
      WITH_TRACER(Flush(isb->packet));
      core_->FreePacket(isb->packet);
    }

    isb->Reset();
    warp->free_isb_idx_q.push(slot);
  }

  for (ItabEntry &itab : warp->itab) {
    itab = ItabEntry{};
  }

  warp->next_inst_order = 0;
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
      LV_STAT(dc_not_available_cycles,
              "Number of active GhOST scheduler cycles without available "
              "Dependence Checkers"),
      LV_STAT(dc_has_dispatch_cycles,
              "Number of active GhOST scheduler cycles that have "
              "dispatched packet"),
      LV_STAT(dc_dispatch_total_counts,
              "Number of total packets DC dispatches"),
      LV_STAT(dispatch_select_failure_cycles,
              "Number of active GhOST scheduler cycles where an available "
              "DC failed to find a dispatchable warp"),
      LV_STAT(dispatch_rejected_warp_counts,
              "Number of warp rejections observed during failed GhOST "
              "dispatch selections"),
      LV_STAT(dispatch_rejected_already_dispatched_warps,
              "Failed-selection warp rejections because the warp already "
              "dispatched this cycle"),
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
      LV_STAT(itab_no_ready_isb_selected_cycles,
              "Number of active GhOST cycles that don't have ready isb entry "
              "when updating ITab"),
      LV_STAT(itab_update_pending_warp_cycles,
              "Number of active GhOST warp cycles awaiting an ITab update"),
      LV_STAT(issue_rate, "GhOST issues per active GhOST scheduler cycle"),
      LV_STAT(ooo_issue_ratio,
              "Fraction of GhOST issues that are out of order"),
      LV_STAT(avg_dc_dispatches_per_ghost_cycle,
              "Average packets that DC dispatches per ghost scheduler cycle"),
      LV_STAT(avg_dc_dispatches_utility,
              "Average packets that DC dispatches per dispatch cycle"),
      LV_STAT(dispatch_select_failure_ratio,
              "Fraction of active GhOST scheduler cycles where an "
              "available DC failed to find a dispatchable warp"),
      LV_STAT(dispatch_rejected_already_dispatched_ratio,
              "Fraction of failed-selection warp rejections already "
              "dispatched this cycle"),
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
      LV_STAT(dc_not_available_ratio,
              "Fraction of active GhOST scheduler cycles without available "
              "Dependence Checkers"),
      LV_STAT(dc_has_dispatch_ratio,
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
      LV_STAT(itab_no_ready_isb_selected_ratio,
              "Fraction of active GhOST cycles that don't have ready isb entry "
              "when updating ITab"),
      LV_STAT(itab_update_pending_warp_ratio,
              "Fraction of active GhOST warp cycles awaiting an ITab update") {
  issue_rate = issue_counts / ghost_cycles;
  ooo_issue_ratio = ooo_issue_counts / issue_counts;
  avg_dc_dispatches_per_ghost_cycle = dc_dispatch_total_counts / ghost_cycles;
  avg_dc_dispatches_utility = dc_dispatch_total_counts / dc_has_dispatch_cycles;
  dispatch_select_failure_ratio = dispatch_select_failure_cycles / ghost_cycles;
  dispatch_rejected_already_dispatched_ratio =
      dispatch_rejected_already_dispatched_warps /
      dispatch_rejected_warp_counts;
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
  dc_not_available_ratio = dc_not_available_cycles / ghost_cycles;
  dc_has_dispatch_ratio = dc_has_dispatch_cycles / ghost_cycles;
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
  itab_no_ready_isb_selected_ratio =
      itab_no_ready_isb_selected_cycles / ghost_cycles;
  itab_update_pending_warp_ratio =
      itab_update_pending_warp_cycles / active_warp_cycles;
}

}  // namespace simtix::pipelined
