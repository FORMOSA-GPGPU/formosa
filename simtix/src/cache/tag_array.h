/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <systemc.h>
#include <tlm_core/tlm_1/tlm_req_rsp/tlm_1_interfaces/tlm_fifo_ifs.h>

#include <cstddef>
#include <optional>
#include <random>
#include <vector>

#include "cache/packet.h"
#include "cache/param.h"

namespace simtix::cache {

class TagArray {
  friend class CacheModuleTester;
  friend class TagArrayTester;

 public:
  explicit TagArray(const Param &p);

  enum class AccessStatus {
    kHit,
    kMiss,
    kLocked,
    kNoVictim,
  };

  /**
   * @brief Process a packet through the tag array.
   *
   * @param packet Packet to process; cache lookup metadata is modified.
   * @return Result of the tag access.
   */
  AccessStatus Process(Packet *packet);

  /**
   * @brief Probe a cache line for the atomic sequencer.
   *
   * @param packet Atomic packet to probe; cache lookup metadata is modified.
   * @return Result of the tag access, including kLocked for locked entries.
   */
  AccessStatus ProbeAtomic(Packet *packet);
  void LockEntry(Location location);
  void UnlockEntry(Location location);
  void MarkDirty(Location location);

  struct DirtyLine {
    Location location;
    uint64_t address = 0;
  };

  size_t EntryCount() const { return tag_array_.size(); }
  std::optional<DirtyLine> ProbeDirtyEntry(size_t index);
  std::optional<DirtyLine> ProbeDirtyLine(uint64_t address);
  void InvalidateEntry(size_t index);
  void InvalidateLine(uint64_t address);

  struct TagEntry {
    bool valid = false;
    bool dirty = false;
    bool locked = false;
    // the tag address stored in this tag entry, aligned to cache block size
    uint64_t tag = 0;
    SC_TIME_DT access_timestamp;  // for replacement policy

    void UpdateTimestamp() { access_timestamp = sc_time_stamp().value(); }
  };

 private:
  struct TagLookupResult {
    size_t way = 0;
    TagEntry *entry = nullptr;
  };

  AccessStatus Probe(Packet *packet, bool respect_lock);
  AccessStatus Refill(Packet *packet);

  uint64_t ToLineAddress(uint64_t address) const {
    return address / config_.block_size_bytes;
  }

  uint64_t ToSetIndex(uint64_t address) const {
    return ToLineAddress(address) % num_sets_;
  }

  TagEntry *GetSet(uint64_t address) {
    const size_t set_index = ToSetIndex(address);
    return &tag_array_[set_index * config_.ways];
  }

  TagEntry &GetEntry(Location location);
  const TagEntry &GetEntry(Location location) const;
  std::optional<TagLookupResult> FindTagEntry(uint64_t address);
  std::optional<TagLookupResult> FindInvalidEntry(TagEntry *set);
  std::optional<TagLookupResult> SelectVictim(TagEntry *set);
  void FillTagEntry(TagEntry *tag_entry, uint64_t address);

  const Param config_;
  const size_t num_sets_;
  std::mt19937_64 random_engine_;

  // Storage layout: [sets][ways].
  std::vector<TagEntry> tag_array_;
};

}  // namespace simtix::cache
