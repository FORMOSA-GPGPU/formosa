// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>

namespace simtix::cache {

/**
 * @brief Generation-protected handle for one reserved victim-buffer entry.
 */
struct VictimReservationId {
  size_t index = 0;
  uint64_t generation = 0;

  bool operator==(const VictimReservationId &other) const {
    return index == other.index && generation == other.generation;
  }
};

/**
 * @brief Move-only capability proving that a dirty victim has reserved space.
 *
 * MSHR owns this token while a refill is outstanding. The refill path either
 * commits it to VictimBuffer for a dirty eviction or cancels it for a clean
 * victim.
 */
class VictimReservation {
 public:
  /** @brief Construct a reservation capability for an allocated entry. */
  explicit VictimReservation(VictimReservationId id) : id_(id) {}
  VictimReservation(VictimReservation &&) = default;
  VictimReservation &operator=(VictimReservation &&) = default;
  VictimReservation(const VictimReservation &) = delete;
  VictimReservation &operator=(const VictimReservation &) = delete;

  /** @return Generation-protected identity of the reserved entry. */
  VictimReservationId id() const { return id_; }

 private:
  VictimReservationId id_;
};

}  // namespace simtix::cache
