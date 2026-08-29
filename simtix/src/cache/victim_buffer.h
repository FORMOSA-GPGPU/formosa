// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <systemc.h>
#include <tlm_core/tlm_1/tlm_req_rsp/tlm_1_interfaces/tlm_fifo_ifs.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "cache/packet.h"
#include "cache/param.h"
#include "cache/victim_reservation.h"

namespace simtix::cache {

class PacketLifecycleIntf;

/**
 * @brief Fixed-capacity buffer for cache-generated dirty-victim writebacks.
 *
 * Entries are reserved before a primary miss is admitted, guaranteeing that a
 * returning refill can always escape the data stage even when the ordinary
 * write buffer is full.
 */
class VictimBuffer : public sc_core::sc_module {
  friend class CacheModuleTester;

 public:
  /** @brief Construct a victim buffer using `param.victim_buffer_entries`. */
  VictimBuffer(sc_module_name name, const Param &param,
               PacketLifecycleIntf &packet_lifecycle);
  ~VictimBuffer() override;

  /**
   * @brief Reserve one entry for a potential dirty eviction.
   * @return Move-only reservation, or `std::nullopt` when full.
   */
  std::optional<VictimReservation> TryReserve();

  /**
   * @brief Convert a reservation into an owned dirty-victim write request.
   * @param reservation Capability returned by TryReserve().
   * @param packet Cache-owned full-line victim write packet.
   */
  void Commit(VictimReservation reservation, Packet *packet);

  /**
   * @brief Release a reservation when refill does not produce a dirty victim.
   * @param reservation Capability returned by TryReserve().
   */
  void Cancel(VictimReservation reservation);

  /** @return true while any entry is reserved, committed, or inflight. */
  bool HasPendingWork() const;
  /** @return Number of entries reserved by outstanding primary misses. */
  size_t ReservedEntryCount() const;
  /** @return Number of writebacks waiting to enter the memory request queue. */
  size_t CommittedEntryCount() const;
  /** @return Number of writebacks awaiting memory completion. */
  size_t InflightEntryCount() const;

  sc_in<bool> SC_NAMED(clock);
  sc_port<tlm::tlm_fifo_put_if<Packet *>> SC_NAMED(mem_req_out);
  sc_port<tlm::tlm_fifo_get_if<Packet *>> SC_NAMED(mem_resp_in);

 private:
  enum class State { kFree, kReserved, kCommitted, kInflight };

  struct Entry {
    // Generation rejects stale reservation handles after a slot is reused.
    State state = State::kFree;
    uint64_t generation = 0;
    Packet *packet = nullptr;
  };

  void Tick();
  void ProcessMemResp();
  void ProcessMemReq();
  Entry &Lookup(VictimReservationId id);
  const Entry &Lookup(VictimReservationId id) const;
  void CheckInvariants() const;

  const Param config_;
  PacketLifecycleIntf &packet_lifecycle_;
  std::vector<Entry> entries_;
  // Memory responses identify inflight entries by their cache-owned payload.
  std::unordered_map<tlm::tlm_generic_payload *, size_t> inflight_map_;
};

}  // namespace simtix::cache
