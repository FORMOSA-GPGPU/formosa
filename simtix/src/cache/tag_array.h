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
    kNoVictim,
    kPendingReplay,  ///< Line is installed but older MSHR packets must go
                     ///< first.
  };

  /**
   * @brief Process a packet through the tag array.
   *
   * @param packet Packet to process; cache lookup metadata is modified.
   * @return Result of the tag access.
   */
  AccessStatus Process(Packet *packet);

  void LockEntry(Location location);
  void UnlockEntry(Location location);
  void MarkDirty(Location location);

  struct DirtyLine {
    Location location;
    uint64_t address = 0;
  };

  size_t EntryCount() const { return tag_array_.size(); }
  bool HasPendingReplayLine() const { return pending_replay_lines_ != 0; }
  std::optional<DirtyLine> ProbeDirtyEntry(size_t index);
  std::optional<DirtyLine> ProbeDirtyLine(uint64_t address);
  void InvalidateEntry(size_t index);
  void InvalidateLine(uint64_t address);

  struct TagEntry {
    bool valid = false;
    bool dirty = false;
    bool locked = false;
    // Set when a refill installs the line and cleared when the last replay of
    // its MSHR entry probes it. Meanwhile core requests cannot hit the line and
    // refills cannot evict it.
    bool pending_replay = false;
    // the tag address stored in this tag entry, aligned to cache block size
    uint64_t tag = 0;
    SC_TIME_DT access_timestamp;  // for replacement policy

    void UpdateTimestamp() { access_timestamp = sc_time_stamp().value(); }
    bool CanBeEvicted() const { return !locked && !pending_replay; }
  };

 private:
  struct TagLookupResult {
    size_t way = 0;
    TagEntry *entry = nullptr;
  };

  AccessStatus Probe(Packet *packet);
  AccessStatus Refill(Packet *packet);

  uint64_t ToLineAddress(uint64_t address) const {
    return address / config_.block_size_bytes;
  }

  uint64_t ToSetIndex(uint64_t address) const {
    return (ToLineAddress(address) >> set_index_shift_) % num_sets_;
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
  size_t pending_replay_lines_ = 0;

  const size_t set_index_shift_;
};

}  // namespace simtix::cache
