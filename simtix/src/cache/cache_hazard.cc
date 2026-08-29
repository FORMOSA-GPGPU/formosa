// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <cassert>

#include "cache.h"

namespace simtix::cache {

/**
 * @brief Check whether an escape-path operation is active for a line.
 *
 * @param line_address Cache-line address to query.
 * @return true while a bypass or write-buffer request for the line is still in
 * flight.
 */
bool Cache::HasLineEscapeHazard(uint64_t line_address) const {
  const auto hazard = line_escape_hazards_.find(line_address);
  return hazard != line_escape_hazards_.end() && hazard->second != 0;
}

/**
 * @brief Check whether a packet must wait for older same-line escape traffic.
 *
 * @param packet Core-origin packet candidate for the tag-array path.
 * @return true when the packet's cache line has an active escape hazard.
 */
bool Cache::ShouldStallForLineEscapeHazard(const Packet *packet) const {
  assert(packet != nullptr);
  return HasLineEscapeHazard(ToLineAddress(packet->GetAddress()));
}

void Cache::IncrementLineEscapeHazardForLine(uint64_t line_address) {
  const size_t count = ++line_escape_hazards_[line_address];
  MarkProgress("hazard_increment");
  SIMTIX_CACHE_LOG_DEBUG(name(), cache_log::Category::kHazard,
                         "event=increment reason=escape_begin pkt=none "
                         "line={:#x} count={}",
                         line_address, count);
}

/**
 * @brief Track a new escape-path operation until its response returns.
 *
 * @param packet Bypass or write-buffer packet that names the affected line.
 */
void Cache::IncrementLineEscapeHazard(const Packet *packet) {
  assert(packet != nullptr);
  const uint64_t line_address = ToLineAddress(packet->GetAddress());
  const size_t count = ++line_escape_hazards_[line_address];
  MarkProgress("hazard_increment");
  SIMTIX_CACHE_LOG_DEBUG(
      name(), cache_log::Category::kHazard,
      "event=increment reason=escape_begin pkt={} parent={} type={} cmd={} "
      "addr={:#x} line={:#x} count={}",
      packet->unique_id, cache_log::PacketParentId(packet),
      cache_log::PacketTypeName(packet->type),
      cache_log::PacketCommandName(packet), cache_log::PacketAddress(packet),
      line_address, count);
}

void Cache::DecrementLineEscapeHazardForLine(uint64_t line_address) {
  auto hazard = line_escape_hazards_.find(line_address);
  assert(hazard != line_escape_hazards_.end());
  assert(hazard->second > 0);
  --hazard->second;
  const size_t count = hazard->second;
  if (hazard->second == 0) {
    line_escape_hazards_.erase(hazard);
  }
  MarkProgress("hazard_decrement");
  SIMTIX_CACHE_LOG_DEBUG(name(), cache_log::Category::kHazard,
                         "event=decrement reason=escape_done pkt=none "
                         "line={:#x} count={}",
                         line_address, count);
}

/**
 * @brief Release one completed escape-path operation for its cache line.
 *
 * @param packet Completed bypass or write-buffer packet.
 */
void Cache::DecrementLineEscapeHazard(const Packet *packet) {
  assert(packet != nullptr);
  const uint64_t line_address = ToLineAddress(packet->GetAddress());
  auto hazard = line_escape_hazards_.find(line_address);
  assert(hazard != line_escape_hazards_.end());
  assert(hazard->second > 0);
  --hazard->second;
  const size_t count = hazard->second;
  if (hazard->second == 0) {
    line_escape_hazards_.erase(hazard);
  }
  MarkProgress("hazard_decrement");
  SIMTIX_CACHE_LOG_DEBUG(
      name(), cache_log::Category::kHazard,
      "event=decrement reason=escape_done pkt={} parent={} type={} cmd={} "
      "addr={:#x} line={:#x} count={}",
      packet->unique_id, cache_log::PacketParentId(packet),
      cache_log::PacketTypeName(packet->type),
      cache_log::PacketCommandName(packet), cache_log::PacketAddress(packet),
      line_address, count);
}

}  // namespace simtix::cache
