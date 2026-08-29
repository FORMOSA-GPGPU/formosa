/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cassert>
#include <cstddef>
#include <unordered_set>

#include "cache/mem_payload.h"
#include "cache/mem_payload_pool.h"
#include "cache/packet.h"
#include "cache/packet_lifecycle_intf.h"
#include "cache/packet_pool.h"

namespace simtix::cache {

class FakePacketLifecycle : public PacketLifecycleIntf {
 public:
  explicit FakePacketLifecycle(size_t payload_size)
      : mem_payload_pool_(payload_size) {}

  Packet *AllocatePacketWithOwnedPayload() override {
    MemPayload *payload = mem_payload_pool_.Acquire();
    Packet *packet = packet_pool_.Acquire(payload);
    released_packets_.erase(packet);
    released_payloads_.erase(payload);
    return packet;
  }

  void ReleasePacket(Packet *packet) override {
    assert(packet != nullptr);
    assert(packet->payload_type() == PayloadType::kCacheOwnedPayload);

    MemPayload *payload = packet->GetCacheOwnedPayload();
    released_packets_.insert(packet);
    released_payloads_.insert(payload);

    mem_payload_pool_.Release(payload);
    packet_pool_.Release(packet);
  }

  bool WasReleased(Packet *packet) const {
    return released_packets_.find(packet) != released_packets_.end();
  }

  bool WasPayloadReleased(MemPayload *payload) const {
    return released_payloads_.find(payload) != released_payloads_.end();
  }

 private:
  MemPayloadPool mem_payload_pool_;
  PacketPool packet_pool_;
  std::unordered_set<Packet *> released_packets_;
  std::unordered_set<MemPayload *> released_payloads_;
};

}  // namespace simtix::cache
