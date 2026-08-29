// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cache/victim_buffer.h"

#include <cassert>

#include "cache/packet_lifecycle_intf.h"

namespace simtix::cache {

VictimBuffer::VictimBuffer(sc_module_name name, const Param &param,
                           PacketLifecycleIntf &packet_lifecycle)
    : sc_module(name), config_(param), packet_lifecycle_(packet_lifecycle) {
  assert(config_.victim_buffer_entries > 0);
  entries_.resize(config_.victim_buffer_entries);
  SC_METHOD(Tick);
  sensitive << clock.pos();
  dont_initialize();
}

VictimBuffer::~VictimBuffer() = default;

std::optional<VictimReservation> VictimBuffer::TryReserve() {
  for (size_t index = 0; index < entries_.size(); ++index) {
    Entry &entry = entries_[index];
    if (entry.state != State::kFree) {
      continue;
    }
    // Allocate a new generation before exposing this reusable physical slot.
    ++entry.generation;
    entry.state = State::kReserved;
    entry.packet = nullptr;
    CheckInvariants();
    return VictimReservation({index, entry.generation});
  }
  return std::nullopt;
}

void VictimBuffer::Commit(VictimReservation reservation, Packet *packet) {
  assert(packet != nullptr);
  assert(packet->type == PacketType::kVictimWriteReq);
  Entry &entry = Lookup(reservation.id());
  assert(entry.state == State::kReserved);
  assert(entry.packet == nullptr);
  entry.packet = packet;
  entry.state = State::kCommitted;
  CheckInvariants();
}

void VictimBuffer::Cancel(VictimReservation reservation) {
  Entry &entry = Lookup(reservation.id());
  assert(entry.state == State::kReserved);
  assert(entry.packet == nullptr);
  entry.state = State::kFree;
  CheckInvariants();
}

bool VictimBuffer::HasPendingWork() const {
  return ReservedEntryCount() + CommittedEntryCount() + InflightEntryCount() !=
         0;
}

size_t VictimBuffer::ReservedEntryCount() const {
  size_t count = 0;
  for (const Entry &entry : entries_) {
    count += entry.state == State::kReserved;
  }
  return count;
}

size_t VictimBuffer::CommittedEntryCount() const {
  size_t count = 0;
  for (const Entry &entry : entries_) {
    count += entry.state == State::kCommitted;
  }
  return count;
}

size_t VictimBuffer::InflightEntryCount() const { return inflight_map_.size(); }

void VictimBuffer::Tick() {
  // Retire completions first so a released entry can be reused promptly.
  ProcessMemResp();
  ProcessMemReq();
  CheckInvariants();
}

void VictimBuffer::ProcessMemResp() {
  if (!mem_resp_in->nb_can_get()) {
    return;
  }
  Packet *packet = nullptr;
  bool success = mem_resp_in->nb_get(packet);
  assert(success);
  assert(packet != nullptr);
  assert(packet->type == PacketType::kVictimWriteResp);

  auto it = inflight_map_.find(packet->GetTlmGp());
  assert(it != inflight_map_.end());
  Entry &entry = entries_.at(it->second);
  assert(entry.state == State::kInflight);
  assert(entry.packet == packet);
  inflight_map_.erase(it);
  entry.packet = nullptr;
  entry.state = State::kFree;
  packet_lifecycle_.ReleasePacket(packet);
}

void VictimBuffer::ProcessMemReq() {
  if (!mem_req_out->nb_can_put()) {
    return;
  }
  for (size_t index = 0; index < entries_.size(); ++index) {
    Entry &entry = entries_[index];
    if (entry.state != State::kCommitted) {
      continue;
    }
    Packet *packet = entry.packet;
    assert(packet != nullptr);
    const bool success = mem_req_out->nb_put(packet);
    assert(success);
    // The entry remains occupied until the corresponding write response.
    entry.state = State::kInflight;
    auto [_, inserted] = inflight_map_.emplace(packet->GetTlmGp(), index);
    assert(inserted);
    return;
  }
}

VictimBuffer::Entry &VictimBuffer::Lookup(VictimReservationId id) {
  Entry &entry = entries_.at(id.index);
  assert(entry.generation == id.generation);
  return entry;
}

const VictimBuffer::Entry &VictimBuffer::Lookup(VictimReservationId id) const {
  const Entry &entry = entries_.at(id.index);
  assert(entry.generation == id.generation);
  return entry;
}

void VictimBuffer::CheckInvariants() const {
  size_t occupied = 0;
  size_t inflight = 0;
  for (const Entry &entry : entries_) {
    if (entry.state != State::kFree) {
      ++occupied;
    }
    if (entry.state == State::kReserved) {
      assert(entry.packet == nullptr);
    } else if (entry.state == State::kCommitted ||
               entry.state == State::kInflight) {
      assert(entry.packet != nullptr);
    }
    inflight += entry.state == State::kInflight;
  }
  assert(occupied <= config_.victim_buffer_entries);
  assert(inflight == inflight_map_.size());
}

}  // namespace simtix::cache
