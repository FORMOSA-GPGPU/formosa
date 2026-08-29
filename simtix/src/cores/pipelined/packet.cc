// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/pipelined/packet.h"

#include <liblv/binding.h>

#include "konata/konata.h"

// Implementing functions required for Konata
namespace simtix::konata {

template <>
uint64_t GetUniqueId<pipelined::Packet>(const pipelined::Packet *packet) {
  return packet->unique_id;
}

template <>
uint32_t GetThreadId<pipelined::Packet>(const pipelined::Packet *packet) {
  return packet->wid;
}

}  // namespace simtix::konata

namespace simtix::pipelined {

LV_BINDING(simtix, Packet)
    .property("wpc", &Packet::wpc, lv::doc("Warp program counter"))
    .property("wid", &Packet::wid, lv::doc("Warp identifier"))
    .property("iword", &Packet::iword,
              lv::doc("Raw instruction word fetched for the packet"));

}  // namespace simtix::pipelined
