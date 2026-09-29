/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/statistics.h>

namespace simtix::cache {

/** Demand-request, cache-outcome, and MSHR-admission statistics. */
struct Stats : lv::stats::Group {
  Metric total_reads;
  Metric total_writes;
  Metric total_atomics;
  Metric total_cacheable_requests;
  Metric total_non_cacheable_requests;
  Metric total_hits;
  Metric total_misses;
  Metric primary_misses;
  Metric secondary_misses;
  Metric total_evictions;
  Formula<Integer> total_requests;
  Formula<Real> hit_rate;

  explicit Stats(const char *name)
      : Group(name),
        LV_STAT(total_reads, "Total number of non-atomic read requests"),
        LV_STAT(total_writes, "Total number of non-atomic write requests"),
        LV_STAT(total_atomics, "Total number of atomic requests"),
        LV_STAT(total_cacheable_requests,
                "Total number of requests routed through the cache"),
        LV_STAT(total_non_cacheable_requests,
                "Total number of requests routed around the cache"),
        LV_STAT(total_hits, "Total number of hits in cache"),
        LV_STAT(total_misses, "Total number of misses in cache"),
        LV_STAT(primary_misses,
                "Number of misses that allocate a new MSHR entry"),
        LV_STAT(secondary_misses,
                "Number of misses merged into an existing MSHR entry"),
        LV_STAT(total_evictions, "Total number of cache lines evicted"),
        LV_STAT(total_requests, "Total number of requests"),
        LV_STAT(hit_rate,
                "The percentage of cache-routed requests hit in the cache") {
    total_requests = total_cacheable_requests + total_non_cacheable_requests;
    hit_rate = (total_hits * 100.0) / total_cacheable_requests;
  }
};

}  // namespace simtix::cache
