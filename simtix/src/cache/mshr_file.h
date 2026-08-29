/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <systemc.h>
#include <tlm_core/tlm_1/tlm_req_rsp/tlm_1_interfaces/tlm_fifo_ifs.h>
#include <tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

#include "cache/packet.h"
#include "cache/param.h"
#include "cache/victim_reservation.h"

namespace simtix::cache {

class PacketLifecycleIntf;

class MshrCore {
  friend class CacheModuleTester;

  class MshrEntry;

 public:
  /**
   * @brief Result of probing a read miss against the current MSHR state.
   *
   * `entry` is non-null only for acceptable probe statuses.
   */
  struct ProbeResult {
    enum class Status {
      kAcceptablePrimaryMiss,    ///< Request can allocate a free MSHR entry.
      kAcceptableSecondaryMiss,  ///< Request can merge into an existing entry.
      kMshrFull,                 ///< No free MSHR entry is available.
      kSubentryFull,             ///< Target entry has no free sub-entry slot.
      kReplaying                 ///< Target entry is draining replay packets.
    };

    Status status;
    MshrEntry *entry;  ///< Valid only for acceptable probe statuses.
  };

  explicit MshrCore(const Param &p);
  ~MshrCore();

  /**
   * @brief Check whether a read miss can be accepted.
   *
   * @pre `packet.GetTlmGp()` is non-null.
   * @param packet The read-miss packet to probe.
   * @return The acceptance status and an entry handle when acceptable.
   */
  ProbeResult ProbeReadMiss(const Packet &packet);

  /** @brief Result of allocating a primary MSHR entry. */
  struct PrimaryMissAllocation {
    uint64_t read_address = 0;
    MshrId mshr_id;
  };

  /**
   * @brief Allocate a probed free entry for a primary miss.
   *
   * @pre `entry` came from a `kAcceptablePrimaryMiss` ProbeResult.
   * @pre `packet` and `packet->GetTlmGp()` are non-null.
   * @param entry The free MSHR entry to allocate.
   * @param packet The original read-miss packet.
   * @param reservation Victim-buffer capacity reserved for this miss.
   * @return Memory read address and generation-protected MSHR identity.
   */
  PrimaryMissAllocation AllocatePrimaryMiss(MshrEntry *entry, Packet *packet,
                                            VictimReservation reservation);

  /**
   * @brief Merge a read miss into an existing MSHR entry.
   *
   * @pre `entry` came from a `kAcceptableSecondaryMiss` ProbeResult.
   * @pre `packet` and `packet->GetTlmGp()` are non-null.
   * @param entry Existing MSHR entry for the packet line.
   * @param packet Read-miss packet to replay after refill.
   */
  void AddSecondaryMiss(MshrEntry *entry, Packet *packet);

  /**
   * @brief Mark an MSHR entry as ready after refill data has committed.
   *
   * @pre The entry no longer owns a victim reservation.
   * @param id Generation-protected MSHR entry identity.
   */
  void NotifyRefill(MshrId id);

  /**
   * @brief Transfer victim reservation ownership out of an MSHR entry.
   * @param id Generation-protected MSHR entry identity.
   * @return Move-only reservation previously owned by the entry.
   */
  VictimReservation TakeVictimReservation(MshrId id);

  /**
   * @brief Check whether any refilled entry still has replay packets to drain.
   *
   * @return true when a replay entry is active or queued.
   */
  bool HasReplayPacket() const;
  bool HasPendingWork() const;

  /**
   * @brief Conservatively check whether a miss for this address can enter MSHR.
   *
   * This is a cache-controller admission query. It lets the controller keep
   * backpressured packets at a stable boundary instead of letting MSHR internal
   * states decide which queue holds the packet.
   *
   * @param address Byte address of the request that may miss in the tag array.
   * @return true when MSHR has capacity to accept the miss.
   */
  bool CanAcceptReadMiss(uint64_t address) const;

  /**
   * @brief Pop the next packet that should enter the replay pipeline.
   *
   * @return The next caller-owned replay packet, or nullptr if none is ready.
   */
  Packet *PopReplayPacket();
  size_t ReadyReplayEntryCount() const;
  size_t ActiveReplayPacketCount() const;

 private:
  class MshrEntry {
   public:
    enum class State {
      kInvalid,  ///< Entry is free and can be allocated for a primary miss.
      kPendingRefill,  ///< Entry is allocated and waiting for a refill
                       ///< notification.
      kReadyToReplay,  ///< Entry has received a refill notification and is
                       ///< ready to replay packets.
      kReplaying,      ///< Entry is currently replaying packets; new secondary
                       ///< misses cannot be merged.
    };

    State state = State::kInvalid;
    size_t index = 0;
    uint64_t line_address = 0;
    uint64_t generation = 0;
    std::deque<Packet *> sub_entries;
    std::optional<VictimReservation> victim_reservation;

    MshrEntry(size_t index, size_t max_sub_entries);
    void Reset();

    // Disable copy and move semantics.
    MshrEntry(const MshrEntry &) = delete;
    MshrEntry(MshrEntry &&) = delete;
    MshrEntry &operator=(const MshrEntry &) = delete;
    MshrEntry &operator=(MshrEntry &&) = delete;

   private:
    const size_t max_sub_entries_;
  };

  struct MshrLookupResult {
    size_t index;
    MshrEntry *entry;
  };

  std::optional<MshrLookupResult> FindMshrEntry(uint64_t line_address);
  MshrEntry &LookupMshrEntry(MshrId id);
  std::optional<MshrLookupResult> FindFreeMshrEntry();
  bool StartNextReplayEntry();

  uint64_t ToLineAddress(uint64_t address) const {
    return address / config_.block_size_bytes;
  }

  const Param config_;

  // Store MSHR entries without relocating them.
  std::deque<MshrEntry> mshr_entries_;

  // At most one refilled entry drains replay packets at a time.
  // MSHR entry now replaying packets, if any. When set, the entry is guaranteed
  // to have replay packets ready.
  std::optional<size_t> replaying_mshr_index_;
  // MSHR-entries that have received refill notifications in arrival order,
  // waiting to start replay.
  std::deque<size_t> ready_to_replay_;
};

class MshrFile : public sc_core::sc_module {
  friend class CacheModuleTester;

 public:
  /**
   * @brief Notification that a cache line refill has reached replay ordering.
   */
  struct RefillNotify {
    uint64_t address = 0;
    uint64_t trace_id = 0;
    MshrId mshr_id;
  };

  enum class AcceptStatus {
    kRejected,
    kAcceptedPrimary,
    kAcceptedSecondary,
  };

  enum class ProbeStatus {
    kRejected,
    kAcceptablePrimary,
    kAcceptableSecondary,
  };

  sc_in<bool> SC_NAMED(clock);
  sc_port<tlm::tlm_fifo_put_if<Packet *>> SC_NAMED(mshr_mem_req);
  sc_port<tlm::tlm_fifo_get_if<RefillNotify>> SC_NAMED(mshr_refill_notify);
  sc_port<tlm::tlm_fifo_put_if<Packet *>> SC_NAMED(mshr_replay);

  MshrFile(sc_module_name name, const Param &param,
           PacketLifecycleIntf &packet_lifecycle);
  bool HasPendingWork() const;
  bool HasReplayWork() const;
  bool CanAcceptReadMiss(uint64_t address) const;

  /**
   * @brief Probe whether a read miss would allocate or merge an MSHR entry.
   * @param packet Read-miss packet to inspect without changing state.
   * @return Primary, secondary, or rejected admission status.
   */
  ProbeStatus ProbeReadMiss(const Packet &packet);

  /**
   * @brief Commit a previously probed read miss into MSHR state.
   *
   * Primary misses allocate an MSHR entry and emit a block read on
   * `mshr_mem_req`; secondary misses merge into an existing pending entry.
   * Rejected misses leave MSHR state unchanged so the caller can keep the
   * packet at its own backpressure boundary.
   *
   * @param packet Core request packet that missed in the tag array.
   * @param reservation Required for primary misses and null for secondary
   * misses. Ownership moves into the allocated MSHR entry on success.
   * @return The accepted miss kind, or `kRejected` when no state changed.
   */
  AcceptStatus TryAcceptReadMiss(Packet *packet,
                                 VictimReservation *reservation = nullptr);

  /** @brief Transfer an entry's victim reservation to the refill path. */
  VictimReservation TakeVictimReservation(MshrId id);

 private:
  void Tick();
  void ProcessRefillNotify();
  void ProcessReplay();
  Packet *AllocateMshrReadReqPacket(Packet *primary_packet,
                                    uint64_t read_address, MshrId mshr_id);

  const Param config_;
  MshrCore core_;
  PacketLifecycleIntf &packet_lifecycle_;
};

}  // namespace simtix::cache
