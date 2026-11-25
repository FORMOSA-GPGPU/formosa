// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cache/write_buffer.h"

#include <algorithm>
#include <cassert>

#include "cache/packet_lifecycle_intf.h"

namespace simtix::cache {

WriteBuffer::WriteBuffer(sc_module_name name, const Param &param,
                         PacketLifecycleIntf &packet_lifecycle)
    : sc_module(name), config_(param), packet_lifecycle_(packet_lifecycle) {
  pending_entries_.reserve(config_.write_buffer_entries);
  inflight_entries_.reserve(config_.write_buffer_entries);
  SC_METHOD(Tick);
  sensitive << clock.pos();
  dont_initialize();
}

WriteBuffer::~WriteBuffer() = default;

bool WriteBuffer::HasPendingWork() const { return Occupancy() != 0; }

size_t WriteBuffer::PendingEntryCount() const {
  return pending_entries_.size();
}

size_t WriteBuffer::InflightEntryCount() const {
  return inflight_entries_.size();
}

void WriteBuffer::Tick() {
  ProcessMemResp();
  ProcessWriteRequest();
  ProcessMemReq();
  CheckInvariants();
}

void WriteBuffer::ProcessWriteRequest() {
  if (!write_buffer_in->nb_can_get()) {
    return;
  }
  if (!HasFreeEntry()) {
    return;
  }

  Packet *packet = nullptr;
  bool success = write_buffer_in->nb_get(packet);
  assert(success);
  assert(packet != nullptr);
  assert(packet->type == PacketType::kMemWriteReq);

  pending_entries_.push_back(packet);
}

void WriteBuffer::ProcessMemReq() {
  if (pending_entries_.empty() || !mem_req_out->nb_can_put()) {
    return;
  }

  Packet *packet = pending_entries_.front();
  pending_entries_.erase(pending_entries_.begin());

  bool success = mem_req_out->nb_put(packet);
  assert(success);
  inflight_entries_.push_back(packet);
}

void WriteBuffer::ProcessMemResp() {
  if (!mem_resp_in->nb_can_get()) {
    return;
  }

  Packet *packet = nullptr;
  bool success = mem_resp_in->nb_get(packet);
  assert(success);
  assert(packet != nullptr);
  assert(packet->type == PacketType::kMemWriteResp);

  // Write responses may return out of order.
  auto it =
      std::find(inflight_entries_.begin(), inflight_entries_.end(), packet);
  assert(it != inflight_entries_.end());
  inflight_entries_.erase(it);

  // Free the completed memory request packet.
  packet_lifecycle_.ReleasePacket(packet);
}

bool WriteBuffer::HasFreeEntry() const {
  return Occupancy() < config_.write_buffer_entries;
}

size_t WriteBuffer::Occupancy() const {
  return pending_entries_.size() + inflight_entries_.size();
}

void WriteBuffer::CheckInvariants() const {
  assert(inflight_entries_.size() <= config_.write_buffer_entries);
  assert(Occupancy() <= config_.write_buffer_entries);
}

}  // namespace simtix::cache
