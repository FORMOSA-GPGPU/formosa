/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/log.h>
#include <systemc.h>
#include <tlm_core/tlm_1/tlm_req_rsp/tlm_1_interfaces/tlm_fifo_ifs.h>

#include <array>
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
    // Set when a refill installs the line and cleared when the last replay of
    // its MSHR entry probes it. Meanwhile core requests cannot hit the line and
    // refills cannot evict it.
    bool pending_replay = false;
    // the tag address stored in this tag entry, aligned to cache block size
    uint64_t tag = 0;
    SC_TIME_DT access_timestamp;  // for replacement policy

    void UpdateTimestamp() { access_timestamp = sc_time_stamp().value(); }
    bool CanBeEvicted() const { return !pending_replay; }
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
    const uint64_t line = ToLineAddress(address) >> set_index_shift_;
    switch (config_.set_index_function) {
      case SetIndexFunction::kModulo:
        return line % num_sets_;
      case SetIndexFunction::kIpoly: {
        // Write line = hi * q + lo with q = set_index_odd_factor_. hi mod P(x)
        // is the XOR of ipoly_powers_ over its set bits and is below
        // 2^degree, so remainder * q + lo covers every set in [0, num_sets_).
        // With q == 1 this is plain IPOLY.
        uint64_t remainder = 0;
        uint64_t hi = line / set_index_odd_factor_;
        for (size_t bit = 0; hi != 0; ++bit, hi >>= 1) {
          if (hi & 1) remainder ^= ipoly_powers_[bit];
        }
        return remainder * set_index_odd_factor_ + line % set_index_odd_factor_;
      }
    }
    lv::Fatal("Invalid cache configuration: unhandled set index function\n");
    return 0;
  }

  TagEntry *GetSet(uint64_t address) {
    const size_t set_index = ToSetIndex(address);
    return &tag_array_[set_index * config_.ways];
  }

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
  size_t set_index_odd_factor_ = 1;
  std::array<uint64_t, 64> ipoly_powers_{};
};

}  // namespace simtix::cache
