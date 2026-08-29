// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cache/write_buffer.h"

#include <tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h>

#include <cassert>

#include "cache/packet_lifecycle_intf.h"

namespace simtix::cache {

WriteBuffer::WriteBuffer(sc_module_name name, const Param &param,
                         PacketLifecycleIntf &packet_lifecycle)
    : sc_module(name), config_(param), packet_lifecycle_(packet_lifecycle) {
  SC_METHOD(Tick);
  sensitive << clock.pos();
  dont_initialize();
}

WriteBuffer::~WriteBuffer() = default;

bool WriteBuffer::HasPendingWork() const { return Occupancy() != 0; }

size_t WriteBuffer::PendingEntryCount() const {
  return pending_entries_.size();
}

size_t WriteBuffer::InflightEntryCount() const { return inflight_map_.size(); }

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
  pending_entries_.pop_front();

  tlm::tlm_generic_payload *gp = packet->GetTlmGp();

  bool success = mem_req_out->nb_put(packet);
  assert(success);
  auto [_, inserted] = inflight_map_.emplace(gp, packet);
  assert(inserted);
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

  tlm::tlm_generic_payload *gp = packet->GetTlmGp();

  auto it = inflight_map_.find(gp);
  assert(it != inflight_map_.end());
  Packet *inflight_packet = it->second;
  assert(packet == inflight_packet);
  inflight_map_.erase(it);

  // Free the completed memory request packet.
  packet_lifecycle_.ReleasePacket(inflight_packet);
}

bool WriteBuffer::HasFreeEntry() const {
  return Occupancy() < config_.write_buffer_entries;
}

size_t WriteBuffer::Occupancy() const {
  return pending_entries_.size() + inflight_map_.size();
}

void WriteBuffer::CheckInvariants() const {
  assert(inflight_map_.size() <= config_.write_buffer_entries);
  assert(Occupancy() <= config_.write_buffer_entries);
}

}  // namespace simtix::cache
