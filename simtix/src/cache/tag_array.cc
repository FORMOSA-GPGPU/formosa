// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "tag_array.h"

#include <liblv/output.h>
#include <tlm_core/tlm_1/tlm_analysis/tlm_analysis_port.h>

#include <cassert>
#include <iterator>

namespace simtix::cache {
namespace {
// W. W. Peterson, Error-Correcting Codes, MIT Press: first entry per degree,
// primitive with the fewest nonzero coefficients. Degrees 4-6 match
// GPGPU-Sim's cache IPOLY; degree 1 is x+1.
constexpr uint64_t kIpolyPolynomials[] = {0,     0x3,   0x7,    0xb,   0x13,
                                          0x25,  0x43,  0x89,   0x11d, 0x211,
                                          0x409, 0x805, 0x1053, 0x201b};

// Multiplies a residue by x modulo P(x), which has the given degree.
constexpr uint64_t MultiplyByX(uint64_t residue, uint64_t poly,
                               unsigned degree) {
  residue <<= 1;
  return (residue >> degree) & 1 ? residue ^ poly : residue;
}

// x generates all nonzero residues modulo P(x) iff its order is 2^degree - 1.
constexpr bool IsPrimitive(uint64_t poly, unsigned degree) {
  const uint64_t period = (uint64_t{1} << degree) - 1;
  uint64_t power = 1;
  for (uint64_t k = 1; k <= period; ++k) {
    power = MultiplyByX(power, poly, degree);
    if (power == 1) return k == period;
  }
  return false;
}

constexpr bool AllIpolyPolynomialsPrimitive() {
  for (unsigned degree = 1; degree < std::size(kIpolyPolynomials); ++degree) {
    if (!IsPrimitive(kIpolyPolynomials[degree], degree)) return false;
  }
  return true;
}

static_assert(AllIpolyPolynomialsPrimitive(),
              "every ipoly polynomial must be primitive");
}  // namespace

TagArray::TagArray(const Param &p)
    : config_(p),
      num_sets_(p.GetNumSets()),
      random_engine_(p.random_seed),
      set_index_shift_(p.set_index_shift) {
  // IPOLY setup. Split num_sets into 2^degree * q with q odd; only the
  // 2^degree part is hashed. Line-address bits are coefficients of a GF(2)
  // polynomial, where addition is XOR, so a line reduces mod P(x) by XOR-ing
  // the residues of its set bits. ipoly_powers_[bit] holds x^bit mod P(x):
  // each left shift multiplies by x, and XOR-ing P(x) when the x^degree term
  // appears reduces the result back below degree.
  if (config_.set_index_function == SetIndexFunction::kIpoly) {
    const unsigned degree = num_sets_ == 0 ? 0 : __builtin_ctzll(num_sets_);
    set_index_odd_factor_ = num_sets_ >> degree;
    if (degree >= std::size(kIpolyPolynomials)) {
      lv::Fatal("Invalid cache configuration: ipoly degree {} exceeds {}\n",
                degree, std::size(kIpolyPolynomials) - 1);
    }
    if (degree != 0) {
      uint64_t power = 1;
      for (auto &entry : ipoly_powers_) {
        entry = power;
        power = MultiplyByX(power, kIpolyPolynomials[degree], degree);
      }
    }
  }
  // initialize the tag array
  tag_array_.resize(num_sets_ * config_.ways);
}

TagArray::AccessStatus TagArray::Process(Packet *packet) {
  assert(packet != nullptr);

  switch (packet->type) {
    case PacketType::kCoreReq:
    case PacketType::kReplay:
      return Probe(packet);
    case PacketType::kRefill:
      return Refill(packet);
    default:
      SC_REPORT_ERROR("TagArray", "Received packet with invalid type");
      break;
  }
  return AccessStatus::kMiss;
}

std::optional<TagArray::DirtyLine> TagArray::ProbeDirtyEntry(size_t index) {
  if (index >= tag_array_.size()) {
    return std::nullopt;
  }

  TagEntry &entry = tag_array_[index];
  if (!entry.valid || !entry.dirty) {
    return std::nullopt;
  }
  assert(!entry.pending_replay);

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
  assert(!lookup->entry->pending_replay);

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
  assert(!entry.pending_replay);
  entry.valid = false;
  entry.dirty = false;
  if (config_.replacement_policy == ReplacementPolicy::kLRU) {
    entry.UpdateTimestamp();
  }
}

void TagArray::InvalidateLine(uint64_t address) {
  auto lookup = FindTagEntry(address);
  if (!lookup) {
    return;
  }

  assert(!lookup->entry->pending_replay);
  lookup->entry->valid = false;
  lookup->entry->dirty = false;
  if (config_.replacement_policy == ReplacementPolicy::kLRU) {
    lookup->entry->UpdateTimestamp();
  }
}

/**
 * @brief Probe the tag array for a core or replay packet.
 *
 * @param packet Core or replay packet to probe.
 * @return AccessStatus indicating hit, miss, or pending replay.
 */
TagArray::AccessStatus TagArray::Probe(Packet *packet) {
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

  // Older MSHR packets must observe the line before any younger core request.
  if (packet->type == PacketType::kCoreReq && tag_entry->pending_replay) {
    return AccessStatus::kPendingReplay;
  }

  if (packet->is_last_replay) {
    assert(packet->type == PacketType::kReplay);
    assert(tag_entry->pending_replay);
    tag_entry->pending_replay = false;
    assert(pending_replay_lines_ > 0);
    --pending_replay_lines_;
  }

  if (config_.replacement_policy == ReplacementPolicy::kLRU) {
    // Update timestamp for LRU replacement policy
    tag_entry->UpdateTimestamp();
  }
  // A write-back hit marks the line dirty here, before the data stage writes
  // it. A refill that evicts the line in between then writes the victim back,
  // and reads it after this packet's write because the data stage keeps tag
  // order. An atomic modifies the line although its payload is a read.
  tag_entry->dirty |= (packet->is_write() || packet->is_atomic) &&
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
    packet->has_victim = false;
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
  packet->has_victim = true;
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
 * @return Selected victim way and entry pointer, or std::nullopt when no way
 * can be evicted yet and the refill must retry later.
 */
std::optional<TagArray::TagLookupResult> TagArray::SelectVictim(TagEntry *set) {
  switch (config_.replacement_policy) {
    case ReplacementPolicy::kRandom: {
      std::vector<size_t> candidate_ways;
      candidate_ways.reserve(config_.ways);
      for (size_t way = 0; way < config_.ways; ++way) {
        assert(set[way].valid &&
               "All entries should be valid when selecting victim");
        if (set[way].CanBeEvicted()) {
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
    if (!tag->CanBeEvicted()) {
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
  tag_entry->pending_replay = true;
  ++pending_replay_lines_;
  tag_entry->tag = ToLineAddress(address);
  tag_entry->UpdateTimestamp();
}

}  // namespace simtix::cache
