// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "tag_array.h"

#include <liblv/output.h>
#include <tlm_core/tlm_1/tlm_analysis/tlm_analysis_port.h>

#include <cassert>

namespace simtix::cache {

TagArray::TagArray(const Param &p)
    : config_(p), num_sets_(p.GetNumSets()), random_engine_(p.random_seed) {
  // initialize the tag array
  tag_array_.resize(num_sets_ * config_.ways);
}

TagArray::AccessStatus TagArray::Process(Packet *packet) {
  assert(packet != nullptr);

  switch (packet->type) {
    case PacketType::kCoreReq:
    case PacketType::kReplay:
      return Probe(packet, false);
    case PacketType::kRefill:
      return Refill(packet);
    default:
      SC_REPORT_ERROR("TagArray", "Received packet with invalid type");
      break;
  }
  return AccessStatus::kMiss;
}

TagArray::AccessStatus TagArray::ProbeAtomic(Packet *packet) {
  assert(packet != nullptr);
  assert(packet->type == PacketType::kCoreReq ||
         packet->type == PacketType::kReplay);
  return Probe(packet, true);
}

void TagArray::LockEntry(Location location) {
  TagEntry &entry = GetEntry(location);
  assert(entry.valid);
  assert(!entry.locked);
  entry.locked = true;
}

void TagArray::UnlockEntry(Location location) {
  TagEntry &entry = GetEntry(location);
  assert(entry.valid);
  assert(entry.locked);
  entry.locked = false;
}

void TagArray::MarkDirty(Location location) {
  TagEntry &entry = GetEntry(location);
  assert(entry.valid);
  entry.dirty = true;
}

std::optional<TagArray::DirtyLine> TagArray::ProbeDirtyEntry(size_t index) {
  if (index >= tag_array_.size()) {
    return std::nullopt;
  }

  TagEntry &entry = tag_array_[index];
  if (!entry.valid || !entry.dirty) {
    return std::nullopt;
  }
  assert(!entry.locked);

  entry.dirty = false;
  if (config_.replacement_policy == ReplacementPolicy::kLRU) {
    entry.UpdateTimestamp();
  }
  return DirtyLine{
      .location =
          Location{.set = index / config_.ways, .way = index % config_.ways},
      .address = entry.tag * config_.block_size_bytes,
  };
}

std::optional<TagArray::DirtyLine> TagArray::ProbeDirtyLine(uint64_t address) {
  auto lookup = FindTagEntry(address);
  if (!lookup || !lookup->entry->dirty) {
    return std::nullopt;
  }
  assert(!lookup->entry->locked);

  lookup->entry->dirty = false;
  if (config_.replacement_policy == ReplacementPolicy::kLRU) {
    lookup->entry->UpdateTimestamp();
  }
  return DirtyLine{
      .location = Location{.set = ToSetIndex(address), .way = lookup->way},
      .address = lookup->entry->tag * config_.block_size_bytes,
  };
}

void TagArray::InvalidateEntry(size_t index) {
  if (index >= tag_array_.size()) {
    return;
  }

  TagEntry &entry = tag_array_[index];
  if (!entry.valid) {
    return;
  }
  assert(!entry.locked);
  entry.valid = false;
  entry.dirty = false;
  entry.locked = false;
  if (config_.replacement_policy == ReplacementPolicy::kLRU) {
    entry.UpdateTimestamp();
  }
}

void TagArray::InvalidateLine(uint64_t address) {
  auto lookup = FindTagEntry(address);
  if (!lookup) {
    return;
  }

  assert(!lookup->entry->locked);
  lookup->entry->valid = false;
  lookup->entry->dirty = false;
  lookup->entry->locked = false;
  if (config_.replacement_policy == ReplacementPolicy::kLRU) {
    lookup->entry->UpdateTimestamp();
  }
}

/**
 * @brief Probe the tag array for a core or replay packet.
 *
 * @param packet Core or replay packet to probe.
 * @param respect_lock Whether to respect the lock status of the entry.
 * @return AccessStatus indicating hit, miss, or locked.
 */
TagArray::AccessStatus TagArray::Probe(Packet *packet, bool respect_lock) {
  const uint64_t address = packet->GetAddress();
  auto lookup = FindTagEntry(address);

  if (!lookup) {
    // Miss
    assert(packet->type != PacketType::kReplay &&
           "Replay packets should always hit");
    packet->is_hit = false;
    return AccessStatus::kMiss;
  }

  // Hit
  auto [way, tag_entry] = lookup.value();

  if (respect_lock && tag_entry->locked) {
    return AccessStatus::kLocked;
  }

  if (config_.replacement_policy == ReplacementPolicy::kLRU) {
    // Update timestamp for LRU replacement policy
    tag_entry->UpdateTimestamp();
  }
  // Write hit policy may update the dirty bit on hit
  tag_entry->dirty |= packet->is_write() &&
                      config_.write_hit_policy == WriteHitPolicy::kWriteBack;

  // Pass the hit information to the packet's meta info
  packet->is_hit = true;
  packet->location = {.set = ToSetIndex(address), .way = way};
  return AccessStatus::kHit;
}

/**
 * @brief Refill one cache line selected by the refill packet.
 *
 * @param packet Refill packet whose address identifies the line to install.
 */
TagArray::AccessStatus TagArray::Refill(Packet *packet) {
  const uint64_t address = packet->GetAddress();
  const size_t set = ToSetIndex(address);
  TagEntry *set_entries = GetSet(address);
  assert(!FindTagEntry(address) && "Refill packet should not hit in the cache");

  auto invalid_entry = FindInvalidEntry(set_entries);
  if (invalid_entry) {
    // Has invalid entry, set packet meta info and fill the entry
    auto [way, tag_entry] = invalid_entry.value();

    packet->location = {.set = set, .way = way};
    packet->is_hit = true;
    packet->is_victim_dirty = false;
    packet->victim_address = 0;
    FillTagEntry(tag_entry, address);
    return AccessStatus::kHit;
  }

  // No invalid entry, need to select a victim to evict

  auto victim = SelectVictim(set_entries);
  if (!victim) {
    packet->is_hit = false;
    return AccessStatus::kNoVictim;
  }

  auto [way, tag_entry] = victim.value();

  packet->location = {.set = set, .way = way};
  packet->is_hit = true;
  packet->is_victim_dirty = tag_entry->dirty;
  packet->victim_address = tag_entry->tag * config_.block_size_bytes;
  FillTagEntry(tag_entry, address);
  return AccessStatus::kHit;
}

/**
 * @brief Find a valid tag entry matching an address.
 *
 * @param address Byte address to look up.
 * @return Matching way and entry pointer, or std::nullopt on miss.
 */
std::optional<TagArray::TagLookupResult> TagArray::FindTagEntry(
    uint64_t address) {
  TagEntry *set = GetSet(address);
  uint64_t line_addr = ToLineAddress(address);

  for (size_t way = 0; way < config_.ways; ++way) {
    TagEntry &tag = set[way];
    if (tag.valid && tag.tag == line_addr) {
      return TagLookupResult{way, &tag};
    }
  }

  return std::nullopt;
}

/**
 * @brief Find an invalid entry in a set.
 *
 * @param set First entry of the set to scan.
 * @return Invalid way and entry pointer, or std::nullopt when the set is full.
 */
std::optional<TagArray::TagLookupResult> TagArray::FindInvalidEntry(
    TagEntry *set) {
  for (size_t way = 0; way < config_.ways; ++way) {
    TagEntry &tag = set[way];
    if (!tag.valid) {
      return TagLookupResult{way, &tag};
    }
  }

  return std::nullopt;
}

/**
 * @brief Select a victim entry from a full set.
 *
 * @param set First entry of the full set.
 * @return Selected victim way and entry pointer, or std::nullopt when all
 * candidate ways are locked and the refill must retry later.
 */
std::optional<TagArray::TagLookupResult> TagArray::SelectVictim(TagEntry *set) {
  switch (config_.replacement_policy) {
    case ReplacementPolicy::kRandom: {
      std::vector<size_t> candidate_ways;
      candidate_ways.reserve(config_.ways);
      for (size_t way = 0; way < config_.ways; ++way) {
        assert(set[way].valid &&
               "All entries should be valid when selecting victim");
        if (!set[way].locked) {
          candidate_ways.push_back(way);
        }
      }
      if (candidate_ways.empty()) {
        return std::nullopt;
      }
      std::uniform_int_distribution<size_t> dist(0, candidate_ways.size() - 1);
      const size_t way = candidate_ways[dist(random_engine_)];
      return TagLookupResult{way, &set[way]};
    }
    case ReplacementPolicy::kLRU:
    case ReplacementPolicy::kFIFO:
      break;
  }

  TagEntry *victim = nullptr;
  SC_TIME_DT min_timestamp = 0;
  for (TagEntry *tag = set, *end = set + config_.ways; tag != end; ++tag) {
    assert(tag->valid && "All entries should be valid when selecting victim");
    if (tag->locked) {
      continue;
    }
    if (victim == nullptr || tag->access_timestamp < min_timestamp) {
      min_timestamp = tag->access_timestamp;
      victim = tag;
    }
  }
  if (victim == nullptr) {
    return std::nullopt;
  }

  return TagLookupResult{static_cast<size_t>(victim - set), victim};
}

/**
 * @brief Install a line tag into an entry.
 *
 * @param tag_entry Entry to update.
 * @param address Byte address in the line being installed.
 */
void TagArray::FillTagEntry(TagEntry *tag_entry, uint64_t address) {
  tag_entry->valid = true;
  tag_entry->dirty = false;  // newly filled line is clean
  tag_entry->locked = false;
  tag_entry->tag = ToLineAddress(address);
  tag_entry->UpdateTimestamp();
}

TagArray::TagEntry &TagArray::GetEntry(Location location) {
  assert(location.set < num_sets_);
  assert(location.way < config_.ways);
  return tag_array_[location.set * config_.ways + location.way];
}

const TagArray::TagEntry &TagArray::GetEntry(Location location) const {
  assert(location.set < num_sets_);
  assert(location.way < config_.ways);
  return tag_array_[location.set * config_.ways + location.way];
}

}  // namespace simtix::cache
