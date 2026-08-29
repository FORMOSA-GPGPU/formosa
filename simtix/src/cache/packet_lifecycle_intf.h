/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "cache/packet.h"

namespace simtix::cache {

class PacketLifecycleIntf {
 public:
  virtual ~PacketLifecycleIntf() = default;

  /**
   * @brief Allocate a packet backed by cache-owned payload storage.
   *
   * @return Newly allocated packet wrapper.
   */
  virtual Packet *AllocatePacketWithOwnedPayload() = 0;

  /**
   * @brief Release a packet allocated through this lifecycle owner.
   *
   * @param packet Packet to release.
   */
  virtual void ReleasePacket(Packet *packet) = 0;
};

}  // namespace simtix::cache
