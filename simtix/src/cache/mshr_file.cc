// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "mshr_file.h"

#include <liblv/output.h>

#include <cassert>

#include "cache/cache_log.h"
#include "cache/packet_lifecycle_intf.h"

namespace simtix::cache {

namespace {

const char *AcceptStatusName(MshrFile::AcceptStatus status) {
  switch (status) {
    case MshrFile::AcceptStatus::kRejected:
      return "rejected";
    case MshrFile::AcceptStatus::kAcceptedPrimary:
      return "accepted_primary";
    case MshrFile::AcceptStatus::kAcceptedSecondary:
      return "accepted_secondary";
  }
  return "unknown";
}

const char *ProbeStatusName(MshrCore::ProbeResult::Status status) {
  switch (status) {
    case MshrCore::ProbeResult::Status::kAcceptablePrimaryMiss:
      return "acceptable_primary";
    case MshrCore::ProbeResult::Status::kAcceptableSecondaryMiss:
      return "acceptable_secondary";
    case MshrCore::ProbeResult::Status::kMshrFull:
      return "mshr_full";
    case MshrCore::ProbeResult::Status::kSubentryFull:
      return "subentry_full";
    case MshrCore::ProbeResult::Status::kReplaying:
      return "replaying";
  }
  return "unknown";
}

}  // namespace

MshrCore::MshrEntry::MshrEntry(size_t index, size_t max_sub_entries)
    : state(State::kInvalid),
      index(index),
      line_address(0),
      max_sub_entries_(max_sub_entries) {}

void MshrCore::MshrEntry::Reset() {
  assert(!victim_reservation.has_value());
  state = State::kInvalid;
  line_address = 0;
  sub_entries.clear();
}

MshrCore::MshrCore(const Param &p) : config_(p) {
  // Initialize the MSHR entries
  for (size_t i = 0; i < config_.mshr_entries; ++i) {
    mshr_entries_.emplace_back(i, config_.mshr_subentries);
  }
}

MshrCore::~MshrCore() = default;

MshrFile::MshrFile(sc_module_name name, const Param &param,
                   PacketLifecycleIntf &packet_lifecycle)
    : sc_core::sc_module(name),
      config_(param),
      core_(param),
      packet_lifecycle_(packet_lifecycle) {
  SC_METHOD(Tick);
  sensitive << clock.pos();
  dont_initialize();
}

MshrCore::ProbeResult MshrCore::ProbeReadMiss(const Packet &packet) {
  const uint64_t line_address = ToLineAddress(packet.GetAddress());

  auto entry_opt = FindMshrEntry(line_address);

  if (entry_opt) {
    if (entry_opt->entry->state == MshrEntry::State::kReadyToReplay ||
        entry_opt->entry->state == MshrEntry::State::kReplaying) {
      return ProbeResult{ProbeResult::Status::kReplaying, nullptr};
    }

    assert(entry_opt->entry->state == MshrEntry::State::kPendingRefill);

    // Secondary miss
    if (entry_opt->entry->sub_entries.size() >= config_.mshr_subentries) {
      // Sub-entry limit reached, cannot add this request to the MSHR entry
      return ProbeResult{ProbeResult::Status::kSubentryFull, nullptr};
    }
    return ProbeResult{ProbeResult::Status::kAcceptableSecondaryMiss,
                       entry_opt->entry};
  }

  // Primary miss
  auto free_entry_opt = FindFreeMshrEntry();
  if (!free_entry_opt) {
    // No free MSHR entry available
    return ProbeResult{ProbeResult::Status::kMshrFull, nullptr};
  } else {
    return ProbeResult{ProbeResult::Status::kAcceptablePrimaryMiss,
                       free_entry_opt.value().entry};
  }
}

MshrCore::PrimaryMissAllocation MshrCore::AllocatePrimaryMiss(
    MshrEntry *entry, Packet *packet, VictimReservation reservation) {
  assert(entry != nullptr);
  assert(entry->state == MshrEntry::State::kInvalid);
  assert(!entry->victim_reservation.has_value());

  ++entry->generation;
  entry->state = MshrEntry::State::kPendingRefill;
  entry->line_address = ToLineAddress(packet->GetAddress());
  entry->sub_entries.push_back(packet);
  entry->victim_reservation.emplace(std::move(reservation));

  return {entry->line_address * config_.block_size_bytes,
          MshrId{entry->index, entry->generation}};
}

void MshrCore::AddSecondaryMiss(MshrEntry *entry, Packet *packet) {
  assert(entry != nullptr);
  assert(entry->state == MshrEntry::State::kPendingRefill);
  assert(entry->line_address == ToLineAddress(packet->GetAddress()));
  assert(entry->sub_entries.size() < config_.mshr_subentries);

  entry->sub_entries.push_back(packet);
}

void MshrCore::NotifyRefill(MshrId id) {
  MshrEntry *entry = &LookupMshrEntry(id);
  assert(entry->state == MshrEntry::State::kPendingRefill);
  assert(!entry->sub_entries.empty());
  assert(!entry->victim_reservation.has_value());

  entry->state = MshrEntry::State::kReadyToReplay;
  ready_to_replay_.push_back(id.index);
}

VictimReservation MshrCore::TakeVictimReservation(MshrId id) {
  MshrEntry &entry = LookupMshrEntry(id);
  assert(entry.state == MshrEntry::State::kPendingRefill);
  assert(entry.victim_reservation.has_value());
  VictimReservation reservation = std::move(*entry.victim_reservation);
  entry.victim_reservation.reset();
  return reservation;
}

bool MshrCore::HasReplayPacket() const {
  return replaying_mshr_index_.has_value() || !ready_to_replay_.empty();
}

size_t MshrCore::ReadyReplayEntryCount() const {
  return ready_to_replay_.size();
}

size_t MshrCore::ActiveReplayPacketCount() const {
  if (!replaying_mshr_index_.has_value()) {
    return 0;
  }
  return mshr_entries_[*replaying_mshr_index_].sub_entries.size();
}

bool MshrCore::HasPendingWork() const {
  if (HasReplayPacket()) {
    return true;
  }
  for (const MshrEntry &entry : mshr_entries_) {
    if (entry.state != MshrEntry::State::kInvalid) {
      return true;
    }
  }
  return false;
}

bool MshrCore::CanAcceptReadMiss(uint64_t address) const {
  const uint64_t line_address = ToLineAddress(address);
  bool has_free_entry = false;

  for (const MshrEntry &entry : mshr_entries_) {
    if (entry.state == MshrEntry::State::kInvalid) {
      has_free_entry = true;
      continue;
    }

    if (entry.line_address != line_address) {
      continue;
    }

    if (entry.state == MshrEntry::State::kPendingRefill) {
      return entry.sub_entries.size() < config_.mshr_subentries;
    }

    assert(entry.state == MshrEntry::State::kReadyToReplay ||
           entry.state == MshrEntry::State::kReplaying);
    return false;
  }

  return has_free_entry;
}

/**
 * @brief Start draining the next refilled entry through the replay pipeline.
 *
 * @return true when a replay entry is active, false when none is ready.
 */
bool MshrCore::StartNextReplayEntry() {
  if (replaying_mshr_index_.has_value()) {
    // A replay entry is already active, do nothing.
    return true;
  }
  if (ready_to_replay_.empty()) {
    // No ready replay entry available.
    return false;
  }

  // Set next entry to replaying state

  const size_t index = ready_to_replay_.front();
  ready_to_replay_.pop_front();

  MshrEntry &entry = mshr_entries_[index];
  assert(entry.state == MshrEntry::State::kReadyToReplay);
  assert(!entry.sub_entries.empty());
  entry.state = MshrEntry::State::kReplaying;
  replaying_mshr_index_ = index;
  return true;
}

Packet *MshrCore::PopReplayPacket() {
  if (!StartNextReplayEntry()) {
    return nullptr;
  }
  assert(replaying_mshr_index_.has_value());

  MshrEntry &entry = mshr_entries_[*replaying_mshr_index_];
  assert(entry.state == MshrEntry::State::kReplaying);

  Packet *packet = entry.sub_entries.front();
  entry.sub_entries.pop_front();
  assert(packet != nullptr);
  packet->type = PacketType::kReplay;

  if (entry.sub_entries.empty()) {
    // Finished replaying all sub-entries for this MSHR entry, reset it and move
    // on.
    entry.Reset();
    replaying_mshr_index_.reset();
  }

  return packet;
}

/**
 * @brief Find an existing MSHR entry for the given line address
 *
 * @param line_address The cache-line number to search for
 * @return The lookup result if found, std::nullopt otherwise
 */
std::optional<MshrCore::MshrLookupResult> MshrCore::FindMshrEntry(
    uint64_t line_address) {
  for (size_t i = 0; i < mshr_entries_.size(); ++i) {
    auto &entry = mshr_entries_[i];
    if (entry.state != MshrEntry::State::kInvalid &&
        entry.line_address == line_address) {
      return MshrLookupResult{i, &entry};
    }
  }
  return std::nullopt;
}

MshrCore::MshrEntry &MshrCore::LookupMshrEntry(MshrId id) {
  MshrEntry &entry = mshr_entries_.at(id.index);
  assert(entry.state != MshrEntry::State::kInvalid);
  assert(entry.generation == id.generation);
  return entry;
}

/**
 * @brief Find a free MSHR entry
 *
 * @return The lookup result for a free entry if available, std::nullopt
 * otherwise
 */
std::optional<MshrCore::MshrLookupResult> MshrCore::FindFreeMshrEntry() {
  for (size_t i = 0; i < mshr_entries_.size(); ++i) {
    auto &entry = mshr_entries_[i];
    if (entry.state == MshrEntry::State::kInvalid) {
      return MshrLookupResult{i, &entry};
    }
  }
  return std::nullopt;
}

void MshrFile::Tick() {
  ProcessRefillNotify();
  ProcessReplay();
}

/**
 * @brief Allocate a cache-owned memory read packet for a primary miss.
 *
 * @param primary_packet Primary miss packet that allocated the MSHR entry.
 * @param read_address Block-aligned memory address to read.
 * @return Cache-owned memory read packet.
 */
Packet *MshrFile::AllocateMshrReadReqPacket(Packet *primary_packet,
                                            uint64_t read_address,
                                            MshrId mshr_id) {
  assert(primary_packet != nullptr);
  assert(primary_packet->type == PacketType::kCoreReq);

  Packet *mem_req_packet = packet_lifecycle_.AllocatePacketWithOwnedPayload();
  assert(mem_req_packet != nullptr);
  mem_req_packet->type = PacketType::kMshrReadReq;
  mem_req_packet->is_atomic = primary_packet->is_atomic;
  mem_req_packet->mshr_id = mshr_id;
  SetTraceParent(mem_req_packet, primary_packet);
  mem_req_packet->GetCacheOwnedPayload()->InitRead(read_address,
                                                   config_.block_size_bytes);
  return mem_req_packet;
}

bool MshrFile::HasPendingWork() const { return core_.HasPendingWork(); }

bool MshrFile::HasReplayWork() const { return core_.HasReplayPacket(); }

bool MshrFile::CanAcceptReadMiss(uint64_t address) const {
  return core_.CanAcceptReadMiss(address);
}

MshrFile::ProbeStatus MshrFile::ProbeReadMiss(const Packet &packet) {
  const auto result = core_.ProbeReadMiss(packet);
  switch (result.status) {
    case MshrCore::ProbeResult::Status::kAcceptablePrimaryMiss:
      return ProbeStatus::kAcceptablePrimary;
    case MshrCore::ProbeResult::Status::kAcceptableSecondaryMiss:
      return ProbeStatus::kAcceptableSecondary;
    default:
      return ProbeStatus::kRejected;
  }
}

MshrFile::AcceptStatus MshrFile::TryAcceptReadMiss(
    Packet *packet, VictimReservation *reservation) {
  assert(packet != nullptr);
  assert(packet->type == PacketType::kCoreReq);

  auto probe_result = core_.ProbeReadMiss(*packet);

  switch (probe_result.status) {
    case MshrCore::ProbeResult::Status::kAcceptablePrimaryMiss: {
      if (reservation == nullptr) {
        return AcceptStatus::kRejected;
      }
      if (!mshr_mem_req->nb_can_put()) {
        SIMTIX_CACHE_LOG_DEBUG(
            name(), cache_log::Category::kMshr,
            "event=accept_read_miss status={} reason=mshr_mem_req_full "
            "pkt={} parent={} type={} cmd={} addr={:#x} line={:#x} "
            "probe_status={}",
            AcceptStatusName(AcceptStatus::kRejected), packet->unique_id,
            cache_log::PacketParentId(packet),
            cache_log::PacketTypeName(packet->type),
            cache_log::PacketCommandName(packet),
            cache_log::PacketAddress(packet),
            cache_log::PacketLineAddress(packet, config_.block_size_bytes),
            ProbeStatusName(probe_result.status));
        return AcceptStatus::kRejected;
      }

      auto allocation = core_.AllocatePrimaryMiss(probe_result.entry, packet,
                                                  std::move(*reservation));
      Packet *mem_req_packet = AllocateMshrReadReqPacket(
          packet, allocation.read_address, allocation.mshr_id);

      const bool success = mshr_mem_req->nb_put(mem_req_packet);
      assert(success);
      SIMTIX_CACHE_LOG_DEBUG(
          name(), cache_log::Category::kMshr,
          "event=accept_read_miss status={} reason=primary pkt={} parent={} "
          "type={} cmd={} addr={:#x} line={:#x} mem_req_pkt={} "
          "mem_req_addr={:#x} probe_status={}",
          AcceptStatusName(AcceptStatus::kAcceptedPrimary), packet->unique_id,
          cache_log::PacketParentId(packet),
          cache_log::PacketTypeName(packet->type),
          cache_log::PacketCommandName(packet),
          cache_log::PacketAddress(packet),
          cache_log::PacketLineAddress(packet, config_.block_size_bytes),
          mem_req_packet->unique_id, allocation.read_address,
          ProbeStatusName(probe_result.status));
      return AcceptStatus::kAcceptedPrimary;
    }
    case MshrCore::ProbeResult::Status::kAcceptableSecondaryMiss: {
      assert(reservation == nullptr);
      core_.AddSecondaryMiss(probe_result.entry, packet);
      SIMTIX_CACHE_LOG_DEBUG(
          name(), cache_log::Category::kMshr,
          "event=accept_read_miss status={} reason=secondary pkt={} parent={} "
          "type={} cmd={} addr={:#x} line={:#x} probe_status={}",
          AcceptStatusName(AcceptStatus::kAcceptedSecondary), packet->unique_id,
          cache_log::PacketParentId(packet),
          cache_log::PacketTypeName(packet->type),
          cache_log::PacketCommandName(packet),
          cache_log::PacketAddress(packet),
          cache_log::PacketLineAddress(packet, config_.block_size_bytes),
          ProbeStatusName(probe_result.status));
      return AcceptStatus::kAcceptedSecondary;
    }
    default:
      SIMTIX_CACHE_LOG_DEBUG(
          name(), cache_log::Category::kMshr,
          "event=accept_read_miss status={} reason={} pkt={} parent={} "
          "type={} cmd={} addr={:#x} line={:#x} probe_status={}",
          AcceptStatusName(AcceptStatus::kRejected),
          ProbeStatusName(probe_result.status), packet->unique_id,
          cache_log::PacketParentId(packet),
          cache_log::PacketTypeName(packet->type),
          cache_log::PacketCommandName(packet),
          cache_log::PacketAddress(packet),
          cache_log::PacketLineAddress(packet, config_.block_size_bytes),
          ProbeStatusName(probe_result.status));
      return AcceptStatus::kRejected;
  }
}

void MshrFile::ProcessRefillNotify() {
  if (!mshr_refill_notify->nb_can_get()) {
    return;
  }

  RefillNotify notify;
  const bool success = mshr_refill_notify->nb_get(notify);
  assert(success);

  core_.NotifyRefill(notify.mshr_id);
  SIMTIX_CACHE_LOG_DEBUG(
      name(), cache_log::Category::kMshr,
      "event=refill_notify reason=accepted addr={:#x} line={:#x} trace_id={} "
      "ready_replay_entries={} active_replay_packets={}",
      notify.address, notify.address / config_.block_size_bytes,
      notify.trace_id, core_.ReadyReplayEntryCount(),
      core_.ActiveReplayPacketCount());
}

VictimReservation MshrFile::TakeVictimReservation(MshrId id) {
  return core_.TakeVictimReservation(id);
}

void MshrFile::ProcessReplay() {
  if (!core_.HasReplayPacket()) {
    return;
  }

  if (!mshr_replay->nb_can_put()) {
    SIMTIX_CACHE_LOG_DEBUG(
        name(), cache_log::Category::kMshr,
        "event=replay_emit status=blocked reason=replay_queue_full "
        "ready_replay_entries={} active_replay_packets={}",
        core_.ReadyReplayEntryCount(), core_.ActiveReplayPacketCount());
    return;
  }

  Packet *replay_packet = core_.PopReplayPacket();
  assert(replay_packet != nullptr);
  const bool success = mshr_replay->nb_put(replay_packet);
  assert(success);
  SIMTIX_CACHE_LOG_DEBUG(
      name(), cache_log::Category::kMshr,
      "event=replay_emit status=accepted reason=ready pkt={} parent={} "
      "type={} cmd={} addr={:#x} line={:#x} ready_replay_entries={} "
      "active_replay_packets={}",
      replay_packet->unique_id, cache_log::PacketParentId(replay_packet),
      cache_log::PacketTypeName(replay_packet->type),
      cache_log::PacketCommandName(replay_packet),
      cache_log::PacketAddress(replay_packet),
      cache_log::PacketLineAddress(replay_packet, config_.block_size_bytes),
      core_.ReadyReplayEntryCount(), core_.ActiveReplayPacketCount());
}

}  // namespace simtix::cache
