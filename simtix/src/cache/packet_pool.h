/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h>

#include "packet.h"
#include "utils/object_pool.h"

namespace simtix::cache {

class PacketPool {
 public:
  PacketPool() = default;

  /**
   * @brief Return a packet wrapper to the pool.
   *
   * @param packet Packet acquired from this pool.
   */
  void Release(Packet *packet) {
    packet->Reset();
    pool_.Release(packet);
  }

  /**
   * @brief Acquire a packet that borrows a core-owned TLM payload.
   *
   * @param payload Borrowed TLM generic payload.
   * @return Reusable packet wrapper for `payload`.
   */
  Packet *Acquire(tlm::tlm_generic_payload *payload) {
    Packet *packet = Acquire();
    packet->SetPayload(payload);
    return packet;
  }

  /**
   * @brief Acquire a packet that references cache-owned payload storage.
   *
   * @param mem_payload Cache-owned memory payload storage.
   * @return Reusable packet wrapper for `mem_payload`.
   */
  Packet *Acquire(MemPayload *mem_payload) {
    Packet *packet = Acquire();
    packet->SetPayload(mem_payload);
    return packet;
  }

 private:
  Packet *Acquire() {
    Packet *packet = pool_.Acquire();
    packet->Reset();
    packet->unique_id = unique_id_counter_++;
    return packet;
  }

  size_t unique_id_counter_ = 0;
  simtix::ObjectPool<Packet> pool_;
};

}  // namespace simtix::cache
