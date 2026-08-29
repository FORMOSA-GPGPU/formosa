/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/output.h>
#include <liblv/schema.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "policies.h"

namespace simtix::cache {

class Cache;

struct NonCacheableEntry {
  uint64_t addr = 0;
  size_t size = 0;

  LV_SCHEMA(Cache, NonCacheableEntry, LV_FIELD(addr, "addr"),
            LV_FIELD(size, "size"))
};

struct Param {
  ReplacementPolicy replacement_policy = ReplacementPolicy::kRandom;
  WriteHitPolicy write_hit_policy = WriteHitPolicy::kWriteThrough;
  WriteMissPolicy write_miss_policy = WriteMissPolicy::kWriteAllocate;

  // cache config
  size_t cache_size_bytes = 4096;
  size_t block_size_bytes = 64;
  size_t ways = 4;
  uint64_t random_seed = 0;

  // MSHR config
  size_t mshr_entries = 4;
  size_t mshr_subentries = 2;

  // Write Buffer config
  size_t write_buffer_entries = 4;

  // Dirty-victim buffer config
  size_t victim_buffer_entries = 4;

  // Pipeline config
  size_t pipeline_queue_size = 2;

  // Non-cacheable regions
  std::vector<NonCacheableEntry> non_cacheable_regions;

  // Atomic requests bypass when false and serialize at this cache when true.
  bool atomic_linearization = false;

  // Trace config
  bool pftrace = false;
  std::optional<std::string> konata_trace_out = std::nullopt;

  /**
   * @brief Compute and validate the number of cache sets.
   *
   * Reports a fatal configuration error when block size or ways are zero, or
   * when the cache size is not exactly divisible by block size times ways.
   *
   * @return Number of sets implied by the cache geometry.
   */
  size_t GetNumSets() const {
    if (block_size_bytes == 0) {
      lv::Fatal("Invalid cache configuration: block size must be non-zero\n");
    }
    if (ways == 0) {
      lv::Fatal("Invalid cache configuration: ways must be non-zero\n");
    }

    const size_t sets = cache_size_bytes / (block_size_bytes * ways);
    if (sets * ways * block_size_bytes != cache_size_bytes) {
      lv::Fatal(
          "Invalid cache configuration: cache size must be equal to num_sets * "
          "num_ways * block_size\n");
    }
    return sets;
  }

  LV_SCHEMA(
      Cache, Param,
      lv::field("size_bytes", &Self::cache_size_bytes, d.cache_size_bytes,
                "Cache size in bytes (legacy alias)"),
      LV_FIELD(cache_size_bytes, "Cache size in bytes"),
      LV_FIELD(block_size_bytes, "Block size in bytes"),
      LV_FIELD(ways, "Number of ways in the cache"),
      LV_FIELD(random_seed, "Seed for random replacement policy"),
      LV_FIELD_ENUM(replacement_policy, "Cache replacement policy",
                    {
                        {ReplacementPolicy::kLRU, "LRU"},
                        {ReplacementPolicy::kFIFO, "FIFO"},
                        {ReplacementPolicy::kRandom, "Random"},
                        {ReplacementPolicy::kLRU, "lru"},
                        {ReplacementPolicy::kFIFO, "fifo"},
                        {ReplacementPolicy::kRandom, "random"},
                    }),
      LV_FIELD_ENUM(write_hit_policy, "Write hit policy",
                    {
                        {WriteHitPolicy::kWriteBack, "WriteBack"},
                        {WriteHitPolicy::kWriteThrough, "WriteThrough"},
                    }),
      LV_FIELD_ENUM(write_miss_policy, "Write miss policy",
                    {
                        {WriteMissPolicy::kWriteAllocate, "WriteAllocate"},
                        {WriteMissPolicy::kWriteNoAllocate, "WriteNoAllocate"},
                    }),
      lv::field("mshrs", &Self::mshr_entries, d.mshr_entries,
                "Number of MSHR entries (legacy alias)"),
      LV_FIELD(mshr_entries, "Number of MSHR entries"),
      LV_FIELD(mshr_subentries, "Number of sub-entries per MSHR entry"),
      LV_FIELD(write_buffer_entries, "Number of write buffer inflight entries"),
      LV_FIELD(victim_buffer_entries,
               "Number of reserved or active dirty-victim entries"),
      LV_FIELD(pipeline_queue_size, "Size of all the pipeline queue"),
      LV_FIELD(non_cacheable_regions, "Non-cacheable regions"),
      LV_FIELD(atomic_linearization,
               "Serialize atomic requests at this cache level"),
      LV_FIELD(pftrace, "Enable Perfetto trace"),
      LV_FIELD(konata_trace_out, "Output path for Konata trace (optional)"));
};

}  // namespace simtix::cache
