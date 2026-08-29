// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/scalar/packet.h"

#include "konata/konata.h"

namespace simtix::konata {

template <>
uint64_t GetUniqueId<scalar::Packet>(const scalar::Packet *packet) {
  return packet->unique_id;
}

template <>
uint32_t GetThreadId<scalar::Packet>(const scalar::Packet *packet) {
  return packet->wid;
}

}  // namespace simtix::konata
