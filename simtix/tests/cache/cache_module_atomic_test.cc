// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cache_module_test_common.h"

namespace {

Param MakeWriteThroughAtomicParam() {
  auto param = MakeAtomicParam(false);
  param.write_hit_policy = WriteHitPolicy::kWriteThrough;
  return param;
}

class CacheModuleAtomicTestRunner : public CacheModuleTestRunnerBase {
 public:
  explicit CacheModuleAtomicTestRunner(sc_core::sc_module_name name)
      : CacheModuleTestRunnerBase(name),
        atomic_bypass_bench_("atomic_bypass_bench",
                             MakeWriteThroughAtomicParam()),
        atomic_peer_bench_("atomic_peer_bench", MakeWriteThroughAtomicParam()),
        atomic_hit_bench_("atomic_hit_bench", MakeAtomicParam(true)),
        atomic_miss_bench_("atomic_miss_bench", MakeAtomicParam(true)),
        atomic_order_bench_("atomic_order_bench",
                            [] {
                              auto param = MakeAtomicParam(true);
                              param.mshr_entries = 3;
                              return param;
                            }()),
        atomic_escape_hazard_bench_("atomic_escape_hazard_bench",
                                    MakeAtomicParam(true)),
        atomic_evict_bench_("atomic_evict_bench", MakeAtomicParam(true)),
        atomic_probe_order_bench_("atomic_probe_order_bench",
                                  MakeMergedWriteAtomicParam(2)),
        atomic_replay_order_bench_("atomic_replay_order_bench",
                                   MakeMergedWriteAtomicParam(3)),
        atomic_full_queue_bench_("atomic_full_queue_bench",
                                 MakeMergedWriteAtomicParam(2, 1)),
        atomic_pending_replay_bench_("atomic_pending_replay_bench", [] {
          auto param = MakeAtomicParam(true);
          param.mshr_subentries = 1;
          return param;
        }()) {
    SC_THREAD(Run);
  }

 private:
  void Run() {
    atomic_bypass_bench_.clock_.write(false);
    atomic_peer_bench_.clock_.write(false);
    atomic_hit_bench_.clock_.write(false);
    atomic_miss_bench_.clock_.write(false);
    atomic_order_bench_.clock_.write(false);
    atomic_escape_hazard_bench_.clock_.write(false);
    atomic_evict_bench_.clock_.write(false);
    atomic_probe_order_bench_.clock_.write(false);
    atomic_replay_order_bench_.clock_.write(false);
    atomic_full_queue_bench_.clock_.write(false);
    atomic_pending_replay_bench_.clock_.write(false);
    wait(sc_core::SC_ZERO_TIME);

    RunBench("atomic_bypass_bench", [this] {
      TestAtomicBypassesWhenLinearizationDisabled();
      TestBypassAtomicsPreserveNeighbor();
      TestBypassAtomicWaitsForReplay();
    });
    RunBench("atomic_hit_bench", [this] {
      TestAtomicLinearizedHitRmw();
      TestAtomicCompareSwap();
      TestSameAddressAtomicsOverlap();
      TestFlushWaitsForLatchedAtomicWrite();
    });
    RunBench("atomic_miss_bench", [this] {
      TestAtomicLinearizedMissRefillReplayRmw();
    });
    RunBench("atomic_order_bench", [this] {
      TestAtomicMissDoesNotHoldOlderOrYoungerTraffic();
    });
    RunBench("atomic_escape_hazard_bench", [this] {
      TestAtomicWaitsForSameLineDirtyVictimWriteback();
    });
    RunBench("atomic_evict_bench", [this] {
      TestRefillEvictingAtomicLineWritesBackResult();
    });
    RunBench("atomic_probe_order_bench", [this] {
      TestAtomicObservesOlderWriteStalledInDataStage(atomic_probe_order_bench_,
                                                     0);
    });
    RunBench("atomic_replay_order_bench", [this] {
      TestAtomicObservesOlderWriteStalledInDataStage(atomic_replay_order_bench_,
                                                     1);
    });
    RunBench("atomic_full_queue_bench", [this] {
      TestAtomicObservesOlderWriteStalledInDataStage(atomic_full_queue_bench_,
                                                     0);
    });
    RunBench("atomic_pending_replay_bench", [this] {
      TestAtomicWaitsForRefilledLineReplay();
    });

    sc_core::sc_stop();
  }

  void TestAtomicBypassesWhenLinearizationDisabled() {
    CacheModuleTester view(atomic_bypass_bench_.cache_);
    const auto old_value = U32Bytes(0x11223344);

    auto *atomic = atomic_bypass_bench_.core_.SendAtomic(
        0x04, U32Bytes(5), simtix::AtomicExtension::Op::kAdd);

    Expect(atomic_bypass_bench_.WaitForMemoryRequests(1),
           "non-linearized atomic reaches memory");
    ExpectReadRequest(atomic_bypass_bench_, 0, 0x04, 4,
                      "non-linearized atomic bypasses as the original read");
    Expect(atomic_bypass_bench_.memory_.RequestHasAtomicExtension(0),
           "non-linearized atomic preserves the atomic extension");
    Expect(!view.CachedLine(0x04).has_value(),
           "non-linearized atomic does not allocate a cache line");

    atomic_bypass_bench_.memory_.RespondAt(0, old_value);
    Expect(atomic_bypass_bench_.WaitForCoreResponses(1),
           "non-linearized atomic response returns to core");
    Expect(atomic_bypass_bench_.core_.HasResponse(atomic),
           "non-linearized atomic receives its original payload back");
    Expect(PayloadData(atomic) == old_value,
           "non-linearized atomic response uses memory data directly");
    Expect(!view.CachedLine(0x04).has_value(),
           "non-linearized atomic response still does not install a line");
    const auto stats = view.Statistics();
    Expect(stats.total_reads == 0,
           "atomic request is not classified as a plain read");
    Expect(stats.total_writes == 0,
           "atomic request is not classified as a plain write");
    Expect(stats.total_atomics == 1,
           "non-linearized atomic is counted as one atomic request");
    Expect(stats.total_cacheable_requests == 0,
           "non-linearized atomic does not use the cache route");
    Expect(stats.total_non_cacheable_requests == 1,
           "non-linearized atomic is counted in the bypass route");
    Expect(stats.total_requests == 1,
           "bypassed atomic contributes to total requests");
  }

  void CompleteInitializedAdd(CacheBench &bench, uint64_t address,
                              std::vector<uint8_t> &memory) {
    CacheModuleTester view(bench.cache_);
    const auto responses = bench.core_.response_count();
    const auto requests = bench.memory_.request_count();
    auto *write = bench.core_.SendWrite(address, U32Bytes(1));
    Expect(bench.WaitForCoreResponses(responses + 1),
           "write-through initialization returns to the core");
    Expect(bench.core_.HasResponse(write),
           "write-through initialization response is observed");
    Expect(bench.WaitForMemoryRequests(requests + 1),
           "initialization reaches memory without a flush or eviction");
    if (bench.memory_.request_count() <= requests) return;
    const auto offset = address - 0x20;
    ExpectWriteRequest(bench, requests, 0x20,
                       LineWriteData(16, offset, U32Bytes(1)),
                       "write-through forwards only the initialized word",
                       LineWriteMask(16, offset, sizeof(uint32_t)));
    auto line = view.CachedLine(address);
    Expect(line.has_value(), "write-through keeps the initialized line cached");
    if (line) {
      Expect(!line->tag.dirty, "write-through leaves the local line clean");
    }
    auto *atomic = bench.core_.SendAtomic(address, U32Bytes(1),
                                          simtix::AtomicExtension::Op::kAdd);
    auto *store = bench.memory_.request(requests);
    const auto store_offset = store->get_address() - 0x20;
    for (size_t i = 0; i < store->get_data_length(); ++i) {
      const auto *mask = store->get_byte_enable_ptr();
      if (mask == nullptr ||
          mask[i % store->get_byte_enable_length()] == TLM_BYTE_ENABLED) {
        memory.at(store_offset + i) = store->get_data_ptr()[i];
      }
    }
    for (size_t i = 0; i < 8; ++i) bench.AdvanceCycle();
    Expect(bench.memory_.request_count() == requests + 1,
           "AMO waits for the pending write-through acknowledgement");
    bench.memory_.RespondAt(requests);
    Expect(bench.WaitForMemoryRequests(requests + 2),
           "AMO issues after write-through completes");
    Expect(bench.memory_.RequestHasAtomicExtension(requests + 1),
           "forwarded AMO retains its operation extension");
    auto *amo = bench.memory_.request(requests + 1);
    Expect(amo->get_address() == address && amo->get_data_length() == 4,
           "forwarded AMO preserves its address and width");
    Expect(amo->get_extension<simtix::AtomicExtension>()->op ==
               simtix::AtomicExtension::Op::kAdd,
           "forwarded AMO preserves its operation");
    const auto word_offset = amo->get_address() - 0x20;
    const auto old = LoadU32(memory, word_offset);
    memory = StoreU32At(memory, word_offset, old + LoadU32(PayloadData(amo)));
    bench.memory_.RespondAt(requests + 1, U32Bytes(old));
    Expect(bench.WaitForCoreResponses(responses + 2),
           "forwarded AMO returns to the core");
    Expect(PayloadData(atomic) == U32Bytes(1),
           "AMO observes the preceding ordinary initialization");
    Expect(!view.CachedLine(address).has_value(),
           "AMO invalidates the stale local cache copy");
    auto *read = bench.core_.SendRead(address, 4);
    Expect(bench.WaitForMemoryRequests(requests + 3),
           "ordinary read after AMO refills the invalidated line");
    bench.memory_.RespondAt(requests + 2, memory);
    Expect(bench.WaitForCoreResponses(responses + 3),
           "refilled ordinary read completes");
    Expect(PayloadData(read) == U32Bytes(2),
           "ordinary read observes the result of the forwarded AMO");
  }

  void TestBypassAtomicsPreserveNeighbor() {
    auto memory = StoreU32At(StoreU32At(Sequence(16, 0x40), 0, 0), 4, 0);
    const auto untouched = memory;
    for (auto *bench : {&atomic_bypass_bench_, &atomic_peer_bench_}) {
      FillLineWithRead(*bench, 0x20, memory);
    }
    CompleteInitializedAdd(atomic_bypass_bench_, 0x20, memory);
    CompleteInitializedAdd(atomic_peer_bench_, 0x24, memory);
    Expect(memory == StoreU32At(StoreU32At(untouched, 0, 2), 4, 2),
           "both atomic objects and untouched neighbors survive write-through");
  }

  void TestBypassAtomicWaitsForReplay() {
    auto &bench = atomic_bypass_bench_;
    const auto requests = bench.memory_.request_count();
    const auto responses = bench.core_.response_count();
    auto *read = bench.core_.SendRead(0x54, 4);
    Expect(bench.WaitForMemoryRequests(requests + 1),
           "older read miss reaches memory");
    auto *atomic = bench.core_.SendAtomic(0x54, U32Bytes(1),
                                          simtix::AtomicExtension::Op::kAdd);
    for (size_t i = 0; i < 8; ++i) bench.AdvanceCycle();
    Expect(bench.memory_.request_count() == requests + 1,
           "forwarded AMO waits for the older read miss");
    bench.memory_.RespondAt(requests, StoreU32At(Sequence(16, 0x60), 4, 17));
    Expect(bench.WaitForCoreResponses(responses + 1, 32),
           "older refill replays before atomic invalidation");
    Expect(PayloadData(read) == U32Bytes(17),
           "older replay retains its refilled value");
    Expect(bench.WaitForMemoryRequests(requests + 2, 32),
           "forwarded AMO issues after the read replay");
    bench.memory_.RespondAt(requests + 1, U32Bytes(17));
    Expect(bench.WaitForCoreResponses(responses + 2),
           "AMO following the replay completes");
    Expect(PayloadData(atomic) == U32Bytes(17),
           "AMO receives the shared value after the read replay");
  }

  void TestAtomicLinearizedHitRmw() {
    CacheModuleTester view(atomic_hit_bench_.cache_);
    const auto block = Sequence(16, 0x10);
    FillLineWithRead(atomic_hit_bench_, 0x00, block);

    const size_t request_count = atomic_hit_bench_.memory_.request_count();
    const size_t response_count = atomic_hit_bench_.core_.response_count();
    const uint32_t old_value = LoadU32(block, 4);
    auto *atomic = atomic_hit_bench_.core_.SendAtomic(
        0x04, U32Bytes(5), simtix::AtomicExtension::Op::kAdd);

    Expect(atomic_hit_bench_.WaitForCoreResponses(response_count + 1),
           "atomic hit RMW returns to core");
    Expect(atomic_hit_bench_.core_.HasResponse(atomic),
           "atomic hit response is observed");
    Expect(PayloadData(atomic) == U32Bytes(old_value),
           "atomic hit returns the old cached value");

    auto line = view.CachedLine(0x04);
    Expect(line.has_value(), "atomic hit keeps the line installed");
    if (line.has_value()) {
      Expect(line->data == StoreU32At(block, 4, old_value + 5),
             "atomic hit writes the new value into the cached line");
      Expect(line->tag.dirty, "atomic hit marks the line dirty");
    }
    for (size_t i = 0; i < 4; ++i) {
      atomic_hit_bench_.AdvanceCycle();
    }
    Expect(atomic_hit_bench_.memory_.request_count() == request_count,
           "atomic hit does not issue a memory request");
    const auto stats = view.Statistics();
    Expect(stats.total_reads == 1,
           "line-fill request remains classified as a plain read");
    Expect(stats.total_atomics == 1,
           "atomic hit is classified separately from reads");
    Expect(stats.total_cacheable_requests == 2,
           "line fill and atomic hit both use the cache route");
    Expect(stats.total_requests == 2,
           "line fill and atomic hit both contribute to total requests");
  }

  void TestAtomicCompareSwap() {
    CacheModuleTester view(atomic_hit_bench_.cache_);
    for (size_t size : {size_t{4}, size_t{8}}) {
      const uint64_t address = size;
      for (bool matches : {false, true}) {
        const auto line = view.CachedLine(address);
        Expect(line.has_value(), "compare-swap starts with a resident line");
        if (!line.has_value()) return;
        const auto before = line->data;
        uint64_t old_value = 0;
        std::memcpy(&old_value, before.data() + address, size);
        const uint64_t desired = 0xfedcba9876543210;
        std::vector<uint8_t> operand(size);
        std::memcpy(operand.data(), &desired, size);
        uint64_t compare_value = matches ? old_value : (old_value ^ 1);
        // AMOCAS.W compares only the low word of its XLEN-sized rd input.
        if (size == 4) compare_value |= uint64_t{0x12345678} << 32;
        const auto responses = atomic_hit_bench_.core_.response_count();
        auto *cas = atomic_hit_bench_.core_.SendAtomic(
            address, operand, simtix::AtomicExtension::Op::kCompareSwap,
            compare_value);
        Expect(atomic_hit_bench_.WaitForCoreResponses(responses + 1),
               "compare-swap returns to the core");
        auto expected = before;
        if (matches) std::memcpy(expected.data() + address, &desired, size);
        const auto after = view.CachedLine(address);
        Expect(after.has_value(), "compare-swap keeps the line resident");
        if (after.has_value()) {
          Expect(after->data == expected,
                 "compare-swap writes only when the comparison matches");
        }
        std::vector<uint8_t> old_bytes(size);
        std::memcpy(old_bytes.data(), &old_value, size);
        Expect(PayloadData(cas) == old_bytes,
               "successful and failed compare-swap return the old value");
      }
    }
  }

  void TestSameAddressAtomicsOverlap() {
    CacheBench &bench = atomic_hit_bench_;
    CacheModuleTester view(bench.cache_);
    const uint64_t address = 0x04;
    const auto line = view.CachedLine(address);
    Expect(line.has_value(), "same-address atomics start with a resident line");
    if (!line.has_value()) return;
    const uint32_t old_value = LoadU32(line->data, 4);

    const size_t response_count = bench.core_.response_count();
    auto *first = bench.core_.SendAtomic(address, U32Bytes(3),
                                         simtix::AtomicExtension::Op::kAdd);
    auto *second = bench.core_.SendAtomic(address, U32Bytes(5),
                                          simtix::AtomicExtension::Op::kAdd);
    bool overlapped = false;
    for (size_t cycle = 0; cycle < 16 && !overlapped; ++cycle) {
      bench.AdvanceCycle();
      overlapped =
          view.HasLatchedAtomicWrite() && view.Queues().tag_array_resp == 1;
    }
    Expect(overlapped,
           "second atomic passes the tag stage while the first waits to "
           "write back");

    Expect(bench.WaitForCoreResponses(response_count + 2, 32),
           "both same-address atomics respond");
    Expect(bench.core_.HasResponse(first) && bench.core_.HasResponse(second),
           "both same-address atomic responses are observed");
    Expect(PayloadData(first) == U32Bytes(old_value),
           "first atomic returns the resident value");
    Expect(PayloadData(second) == U32Bytes(old_value + 3),
           "second atomic returns the first atomic's result");
    const auto after = view.CachedLine(address);
    Expect(after.has_value() && LoadU32(after->data, 4) == old_value + 8,
           "line holds the result of both atomics");
  }

  void TestFlushWaitsForLatchedAtomicWrite() {
    CacheBench &bench = atomic_hit_bench_;
    CacheModuleTester view(bench.cache_);
    const uint64_t address = 0x04;
    const auto line = view.CachedLine(address);
    Expect(line.has_value(), "flush test starts with a resident line");
    if (!line.has_value()) return;
    const uint32_t old_value = LoadU32(line->data, 4);
    const auto expected = StoreU32At(line->data, 4, old_value + 7);
    const size_t request_count = bench.memory_.request_count();
    const size_t response_count = bench.core_.response_count();

    auto *atomic = bench.core_.SendAtomic(address, U32Bytes(7),
                                          simtix::AtomicExtension::Op::kAdd);
    bool atomic_queued = false;
    for (size_t cycle = 0; cycle < 8 && !atomic_queued; ++cycle) {
      bench.AdvanceCycle();
      atomic_queued = view.Queues().core_req == 1;
    }
    Expect(atomic_queued, "atomic enters the core request queue");
    Expect(SendCacheCommand(bench, kFlushOpcode, 0x00, 16),
           "flush command is accepted behind the atomic");
    bench.AdvanceCycle();
    const bool atomic_tagged = view.Queues().tag_array_resp == 1;
    Expect(atomic_tagged, "atomic passes the tag stage before the flush");

    // Core response backpressure holds the atomic in the write latch while the
    // flush checks whether the pipeline has drained.
    view.FillCoreResponseQueueWithTestPackets(0x1000);
    view.AccessDataArrayStage();
    wait(sc_core::SC_ZERO_TIME);
    Expect(view.HasLatchedAtomicWrite(), "atomic waits in the write latch");
    view.AdvanceCommandSequencer();
    wait(sc_core::SC_ZERO_TIME);
    view.AdvanceCommandSequencer();
    wait(sc_core::SC_ZERO_TIME);
    view.ClearCoreResponseQueue();
    wait(sc_core::SC_ZERO_TIME);

    Expect(bench.WaitForCoreResponses(response_count + 1, 32),
           "atomic responds after the backpressure clears");
    Expect(PayloadData(atomic) == U32Bytes(old_value),
           "atomic returns the resident value");
    Expect(bench.WaitForMemoryRequests(request_count + 1, 64),
           "flush writes back the atomic line");
    ExpectWriteRequest(bench, request_count, 0x00, expected,
                       "flush writes back the atomic result",
                       AllEnabledMask(expected.size()));
    bench.memory_.RespondAt(request_count);
    Expect(WaitForCacheCommandResponse(bench, 64), "flush completes");
    const auto after = view.CachedLine(address);
    Expect(after.has_value() && !after->tag.dirty,
           "flush leaves the atomic line clean");
  }

  void TestAtomicLinearizedMissRefillReplayRmw() {
    CacheModuleTester view(atomic_miss_bench_.cache_);
    const uint64_t address = 0x24;
    const auto block = Sequence(16, 0x40);
    const uint32_t old_value = LoadU32(block, 4);

    auto *atomic = atomic_miss_bench_.core_.SendAtomic(
        address, U32Bytes(7), simtix::AtomicExtension::Op::kAdd);
    Expect(atomic_miss_bench_.WaitForMemoryRequests(1),
           "linearized atomic miss reaches memory through MSHR");
    ExpectReadRequest(atomic_miss_bench_, 0, 0x20, 16,
                      "linearized atomic miss issues a block refill read");
    Expect(!atomic_miss_bench_.memory_.RequestHasAtomicExtension(0),
           "linearized atomic miss uses a cache-owned refill read");

    atomic_miss_bench_.memory_.RespondAt(0, block);
    Expect(atomic_miss_bench_.WaitForCoreResponses(1, 32),
           "linearized atomic miss replays after refill and returns");
    Expect(atomic_miss_bench_.core_.HasResponse(atomic),
           "linearized atomic miss response is observed");
    Expect(PayloadData(atomic) == U32Bytes(old_value),
           "linearized atomic miss returns the old refilled value");

    auto line = view.CachedLine(address);
    Expect(line.has_value(), "linearized atomic miss allocates the cache line");
    if (line.has_value()) {
      Expect(line->data == StoreU32At(block, 4, old_value + 7),
             "linearized atomic miss updates the refilled cache line");
      Expect(line->tag.dirty, "linearized atomic miss marks the line dirty");
    }
    for (size_t i = 0; i < 4; ++i) {
      atomic_miss_bench_.AdvanceCycle();
    }
    Expect(atomic_miss_bench_.memory_.request_count() == 1,
           "linearized atomic miss does not emit a full-line atomic write");
    const auto stats = view.Statistics();
    Expect(stats.total_reads == 0,
           "atomic miss is not classified as a plain read");
    Expect(stats.total_atomics == 1,
           "atomic miss is counted as one atomic request");
    Expect(stats.total_cacheable_requests == 1,
           "linearized atomic miss uses the cache route");
    Expect(stats.total_non_cacheable_requests == 0,
           "linearized atomic miss does not use the bypass route");
    Expect(stats.total_requests == 1,
           "atomic miss contributes to total requests");
    Expect(stats.primary_misses == 1,
           "atomic miss allocates one primary MSHR entry");
    Expect(stats.secondary_misses == 0,
           "single atomic miss does not merge into an MSHR entry");

    auto *younger_read = atomic_miss_bench_.core_.SendRead(address, 4);
    Expect(atomic_miss_bench_.WaitForCoreResponses(2, 32),
           "read after the replayed atomic hits its released line");
    Expect(atomic_miss_bench_.core_.HasResponse(younger_read),
           "read after the replayed atomic is observed");
    Expect(PayloadData(younger_read) == U32Bytes(old_value + 7),
           "read after the replayed atomic observes its result");
  }

  void TestAtomicMissDoesNotHoldOlderOrYoungerTraffic() {
    CacheBench &bench = atomic_order_bench_;
    const auto older_block = Sequence(16, 0x20);
    const auto atomic_block = Sequence(16, 0x80);
    const auto younger_block = Sequence(16, 0xC0);

    auto *older = bench.core_.SendRead(0x00, 4);
    Expect(bench.WaitForMemoryRequests(1), "older miss reaches memory first");
    ExpectReadRequest(bench, 0, 0x00, 16,
                      "older miss issues the first refill read");

    auto *atomic = bench.core_.SendAtomic(0x10, U32Bytes(1),
                                          simtix::AtomicExtension::Op::kAdd);
    Expect(bench.WaitForMemoryRequests(2, 32),
           "atomic miss issues without draining unrelated older miss");
    ExpectReadRequest(bench, 1, 0x10, 16,
                      "atomic miss issues before the younger read");

    auto *younger = bench.core_.SendRead(0x20, 4);
    Expect(bench.WaitForMemoryRequests(3, 32),
           "younger read issues while the atomic miss is unresolved");
    ExpectReadRequest(bench, 2, 0x20, 16,
                      "younger read is the third memory request");

    bench.memory_.RespondAt(2, younger_block);
    Expect(bench.WaitForCoreResponses(1, 32),
           "younger read responds after its refill");
    Expect(bench.core_.HasResponse(younger) && !bench.core_.HasResponse(atomic),
           "younger read responds while the atomic miss is unresolved");

    bench.memory_.RespondAt(1, atomic_block);
    Expect(bench.WaitForCoreResponses(2, 32),
           "atomic response can return before unrelated older miss");
    Expect(bench.core_.HasResponse(atomic),
           "atomic ordering response is observed");
    Expect(PayloadData(atomic) == Slice(atomic_block, 0, 4),
           "atomic returns the refilled value");

    bench.memory_.RespondAt(0, older_block);
    Expect(bench.WaitForCoreResponses(3, 32), "older read completes");
    Expect(bench.core_.HasResponse(older), "older miss response is observed");
  }

  void TestAtomicWaitsForSameLineDirtyVictimWriteback() {
    CacheModuleTester view(atomic_escape_hazard_bench_.cache_);
    const uint64_t victim_address = 0x00;
    const uint64_t replacement_address = 0x40;
    const auto original_block = Sequence(16, 0x30);
    const auto patch = U32Bytes(0x11223344);
    const auto dirty_block = WithPatch(original_block, 4, patch);
    const auto replacement_block = Sequence(16, 0x70);

    FillLineWithRead(atomic_escape_hazard_bench_, victim_address,
                     original_block);

    auto *write = atomic_escape_hazard_bench_.core_.SendWrite(0x04, patch);
    Expect(atomic_escape_hazard_bench_.WaitForCoreResponses(2),
           "write-back hit responds before the victim is evicted");
    Expect(atomic_escape_hazard_bench_.core_.HasResponse(write),
           "write-back hit response is observed");
    auto line = view.CachedLine(victim_address);
    Expect(line.has_value(), "victim line remains installed before eviction");
    if (line.has_value()) {
      Expect(line->data == dirty_block,
             "write-back hit updates the cached victim line");
      Expect(line->tag.dirty, "write-back hit marks the victim dirty");
    }

    auto *replacement =
        atomic_escape_hazard_bench_.core_.SendRead(replacement_address, 4);
    Expect(atomic_escape_hazard_bench_.WaitForMemoryRequests(2),
           "replacement read miss reaches memory");
    atomic_escape_hazard_bench_.memory_.RespondAt(1, replacement_block);
    Expect(atomic_escape_hazard_bench_.WaitForCoreResponses(3, 32),
           "replacement read responds after refill");
    Expect(atomic_escape_hazard_bench_.core_.HasResponse(replacement),
           "replacement response is observed");
    Expect(atomic_escape_hazard_bench_.WaitForMemoryRequests(3, 32),
           "dirty victim writeback reaches memory");
    ExpectWriteRequest(atomic_escape_hazard_bench_, 2, victim_address,
                       dirty_block,
                       "dirty victim writeback preserves evicted line data",
                       AllEnabledMask(dirty_block.size()));

    auto *atomic = atomic_escape_hazard_bench_.core_.SendAtomic(
        0x04, U32Bytes(1), simtix::AtomicExtension::Op::kAdd);
    for (size_t cycle = 0; cycle < 8; ++cycle) {
      atomic_escape_hazard_bench_.AdvanceCycle();
    }
    Expect(atomic_escape_hazard_bench_.memory_.request_count() == 3,
           "same-line atomic waits for dirty victim writeback response");

    atomic_escape_hazard_bench_.memory_.RespondAt(2);
    Expect(atomic_escape_hazard_bench_.WaitForMemoryRequests(4, 32),
           "atomic miss issues after same-line writeback completes");
    ExpectReadRequest(atomic_escape_hazard_bench_, 3, victim_address, 16,
                      "atomic miss refetches the written-back victim line");

    atomic_escape_hazard_bench_.memory_.RespondAt(3, dirty_block);
    Expect(atomic_escape_hazard_bench_.WaitForCoreResponses(4, 32),
           "atomic response returns after refill and RMW");
    Expect(atomic_escape_hazard_bench_.core_.HasResponse(atomic),
           "same-line atomic response is observed");
    Expect(PayloadData(atomic) == patch,
           "same-line atomic returns the value from the written-back line");
  }

  void TestRefillEvictingAtomicLineWritesBackResult() {
    CacheBench &bench = atomic_evict_bench_;
    CacheModuleTester view(bench.cache_);
    const uint64_t atomic_address = 0x04;
    const uint64_t replacement_address = 0x40;
    const auto block = Sequence(16, 0x50);
    const auto replacement_block = Sequence(16, 0xA0);
    const uint32_t old_value = LoadU32(block, 4);

    FillLineWithRead(bench, 0x00, block);
    auto *replacement = bench.core_.SendRead(replacement_address, 4);
    Expect(bench.WaitForMemoryRequests(2), "replacement miss reaches memory");

    const size_t response_count = bench.core_.response_count();
    auto *atomic = bench.core_.SendAtomic(atomic_address, U32Bytes(1),
                                          simtix::AtomicExtension::Op::kAdd);
    bool atomic_tagged = false;
    for (size_t cycle = 0; cycle < 8 && !atomic_tagged; ++cycle) {
      bench.AdvanceCycle();
      atomic_tagged = view.Queues().tag_array_resp == 1;
    }
    Expect(atomic_tagged, "atomic hit passes the tag stage");

    // The refill takes the tag stage in the cycle the atomic reads the line,
    // so it evicts the line before the atomic writes the line back.
    bench.memory_.RespondAt(1, replacement_block);
    for (size_t delta = 0; delta < 8 && !view.HasMemoryResponse(); ++delta) {
      wait(sc_core::SC_ZERO_TIME);
    }
    view.AcceptMemResponse();
    wait(sc_core::SC_ZERO_TIME);
    view.AccessDataArrayStage();
    view.AccessTagArrayStage();
    wait(sc_core::SC_ZERO_TIME);
    Expect(!view.CachedLine(atomic_address).has_value() &&
               bench.core_.response_count() == response_count,
           "refill evicts the line before the atomic writes it back");

    Expect(bench.WaitForCoreResponses(response_count + 2, 32),
           "atomic and replacement read respond");
    Expect(bench.core_.HasResponse(atomic), "atomic response is observed");
    Expect(PayloadData(atomic) == U32Bytes(old_value),
           "atomic returns the value read before the eviction");
    Expect(bench.core_.HasResponse(replacement),
           "replacement response is observed");
    Expect(PayloadData(replacement) == Slice(replacement_block, 0, 4),
           "replacement read returns the refilled value");

    Expect(bench.WaitForMemoryRequests(3, 32),
           "evicted atomic line is written back");
    ExpectWriteRequest(bench, 2, 0x00, StoreU32At(block, 4, old_value + 1),
                       "victim writeback carries the atomic result",
                       AllEnabledMask(block.size()));
    const auto line = view.CachedLine(replacement_address);
    Expect(line.has_value(), "replacement line is installed");
    if (line.has_value()) {
      Expect(line->data == replacement_block,
             "atomic write-back does not change the replacement line");
      Expect(!line->tag.dirty,
             "atomic write-back does not dirty the replacement line");
    }
    bench.memory_.RespondAt(2);
    for (size_t cycle = 0; cycle < 4; ++cycle) {
      bench.AdvanceCycle();
    }
  }

  /**
   * @brief Build a cache where a read and a write share one MSHR entry.
   *
   * With two sub-entries the atomic keeps probing because the entry is full;
   * with three it merges and waits for its own replay. A single-entry queue
   * leaves the atomic no slot while the write waits in the data stage.
   */
  static Param MakeMergedWriteAtomicParam(size_t mshr_subentries,
                                          size_t pipeline_queue_size = 2) {
    auto param = MakeAtomicParam(true);
    param.mshr_subentries = mshr_subentries;
    param.pipeline_queue_size = pipeline_queue_size;
    return param;
  }

  void TestAtomicObservesOlderWriteStalledInDataStage(
      CacheBench &bench, int atomic_replays_in_queue) {
    CacheModuleTester view(bench.cache_);
    const uint64_t access_address = 0x04;
    const auto block = Sequence(16, 0x10);
    const uint32_t written_value = 5;

    auto *read = bench.core_.SendRead(access_address, 4);
    Expect(bench.WaitForMemoryRequests(1), "read miss reaches memory");
    auto *write =
        bench.core_.SendWrite(access_address, U32Bytes(written_value));
    auto *atomic = bench.core_.SendAtomic(access_address, U32Bytes(1),
                                          simtix::AtomicExtension::Op::kAdd);
    for (size_t cycle = 0; cycle < 8; ++cycle) {
      bench.AdvanceCycle();
    }
    bench.memory_.RespondAt(0, block);

    // Stop once the write has passed the tag stage but not the data stage.
    bool write_in_data_stage = false;
    for (size_t cycle = 0; cycle < 32 && !write_in_data_stage; ++cycle) {
      bench.AdvanceCycle();
      const auto queues = view.Queues();
      write_in_data_stage = view.Mshr().tracked_sub_entries == 0 &&
                            queues.tag_array_resp == 1 &&
                            queues.mshr_file_replay == atomic_replays_in_queue;
    }
    Expect(write_in_data_stage, "write waits at the data-stage head");
    const bool read_response_queued = view.Queues().core_resp == 1;

    // Core response backpressure holds the write there while the atomic runs.
    view.FillCoreResponseQueueWithTestPackets(0x1000);
    view.AccessDataArrayStage();
    view.AccessTagArrayStage();
    wait(sc_core::SC_ZERO_TIME);
    view.AccessDataArrayStage();
    if (read_response_queued) {
      view.SendCoreResponse();
    }
    view.ClearCoreResponseQueue();
    wait(sc_core::SC_ZERO_TIME);

    Expect(bench.WaitForCoreResponses(3, 64),
           "read, write, and atomic complete");
    Expect(bench.core_.HasResponse(read), "read response is observed");
    Expect(bench.core_.HasResponse(write), "write response is observed");
    Expect(bench.core_.HasResponse(atomic), "atomic response is observed");
    Expect(PayloadData(atomic) == U32Bytes(written_value),
           "atomic reads the value of the older write");

    auto *younger_read = bench.core_.SendRead(access_address, 4);
    for (size_t cycle = 0; cycle < 32 && !bench.core_.HasResponse(younger_read);
         ++cycle) {
      bench.AdvanceCycle();
    }
    Expect(bench.core_.HasResponse(younger_read),
           "read after the atomic responds");
    Expect(PayloadData(younger_read) == U32Bytes(written_value + 1),
           "atomic result is applied on top of the older write");
  }

  void TestAtomicWaitsForRefilledLineReplay() {
    CacheBench &bench = atomic_pending_replay_bench_;
    const uint64_t access_address = 0x04;
    const auto block = Sequence(16, 0x10);
    const auto old_value = Slice(block, 4, 4);

    auto *read = bench.core_.SendRead(access_address, 4);
    Expect(bench.WaitForMemoryRequests(1), "read miss reaches memory");

    // The read holds the only sub-entry, so the atomic keeps probing until the
    // refill installs the line it is waiting to replay.
    auto *atomic = bench.core_.SendAtomic(access_address, U32Bytes(1),
                                          simtix::AtomicExtension::Op::kAdd);
    for (size_t cycle = 0; cycle < 8; ++cycle) {
      bench.AdvanceCycle();
    }
    Expect(!bench.core_.HasResponse(atomic),
           "atomic waits for the refill of the older read");

    bench.memory_.RespondAt(0, block);
    Expect(bench.WaitForCoreResponses(2, 64),
           "read and atomic complete after the refill");
    Expect(bench.core_.HasResponse(read), "read response is observed");
    Expect(PayloadData(read) == old_value, "read returns the refilled value");
    Expect(bench.core_.HasResponse(atomic), "atomic response is observed");
    Expect(PayloadData(atomic) == old_value, "atomic reads the refilled value");
    Expect(bench.memory_.request_count() == 1,
           "atomic reuses the refilled line");

    auto *younger_read = bench.core_.SendRead(access_address, 4);
    for (size_t cycle = 0; cycle < 32 && !bench.core_.HasResponse(younger_read);
         ++cycle) {
      bench.AdvanceCycle();
    }
    Expect(bench.core_.HasResponse(younger_read),
           "read after the atomic responds");
    Expect(PayloadData(younger_read) == U32Bytes(LoadU32(block, 4) + 1),
           "read after the atomic observes its result");
  }

  CacheBench atomic_bypass_bench_;
  CacheBench atomic_peer_bench_;
  CacheBench atomic_hit_bench_;
  CacheBench atomic_miss_bench_;
  CacheBench atomic_order_bench_;
  CacheBench atomic_escape_hazard_bench_;
  CacheBench atomic_evict_bench_;
  CacheBench atomic_probe_order_bench_;
  CacheBench atomic_replay_order_bench_;
  CacheBench atomic_full_queue_bench_;
  CacheBench atomic_pending_replay_bench_;
};

}  // namespace

int sc_main(int, char *[]) {
  CacheModuleAtomicTestRunner tester("tester");
  sc_core::sc_start();
  return tester.failed() ? 1 : 0;
}
