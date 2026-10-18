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
        atomic_bypass_starvation_bench_("atomic_bypass_starvation_bench",
                                        MakeAtomicNonCacheableParam()),
        atomic_order_bench_("atomic_order_bench", MakeAtomicParam(true)),
        atomic_escape_hazard_bench_("atomic_escape_hazard_bench",
                                    MakeAtomicParam(true)),
        atomic_locked_refill_bench_("atomic_locked_refill_bench",
                                    MakeAtomicParam(true)) {
    SC_THREAD(Run);
  }

 private:
  void Run() {
    atomic_bypass_bench_.clock_.write(false);
    atomic_peer_bench_.clock_.write(false);
    atomic_hit_bench_.clock_.write(false);
    atomic_miss_bench_.clock_.write(false);
    atomic_bypass_starvation_bench_.clock_.write(false);
    atomic_order_bench_.clock_.write(false);
    atomic_escape_hazard_bench_.clock_.write(false);
    atomic_locked_refill_bench_.clock_.write(false);
    wait(sc_core::SC_ZERO_TIME);

    RunBench("atomic_bypass_bench", [this] {
      TestAtomicBypassesWhenLinearizationDisabled();
      TestBypassAtomicsPreserveNeighbor();
      TestBypassAtomicWaitsForReplay();
    });
    RunBench("atomic_hit_bench", [this] {
      TestAtomicLinearizedHitRmw();
      TestAtomicCompareSwap();
    });
    RunBench("atomic_miss_bench", [this] {
      TestAtomicLinearizedMissRefillReplayRmw();
    });
    RunBench("atomic_bypass_starvation_bench", [this] {
      TestAtomicBlockedBypassDoesNotStarveMshrRead();
    });
    RunBench("atomic_order_bench", [this] {
      TestAtomicDoesNotDrainUnrelatedOlderMissAndBlocksYoungerTraffic();
    });
    RunBench("atomic_escape_hazard_bench", [this] {
      TestAtomicWaitsForSameLineDirtyVictimWriteback();
    });
    RunBench("atomic_locked_refill_bench", [this] {
      TestLockedVictimRefillStallsAndRecovers();
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

    bool saw_lock = false;
    for (size_t cycle = 0; cycle < 8; ++cycle) {
      atomic_hit_bench_.AdvanceCycle();
      auto line = view.CachedLine(0x04);
      if (line.has_value() && line->tag.locked) {
        saw_lock = true;
        Expect(atomic_hit_bench_.core_.response_count() == response_count,
               "atomic hit does not respond during the read half of RMW");
        break;
      }
    }
    Expect(saw_lock, "atomic hit locks the tag entry during RMW");

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
      Expect(!line->tag.locked, "atomic hit unlocks the tag entry");
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
      Expect(!line->tag.locked, "linearized atomic miss unlocks the tag entry");
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
  }

  void TestAtomicBlockedBypassDoesNotStarveMshrRead() {
    CacheModuleTester view(atomic_bypass_starvation_bench_.cache_);
    const uint64_t atomic_address = 0x24;
    const uint64_t bypass_address = 0x40;
    const auto atomic_block = Sequence(16, 0x80);
    const auto bypass_data = Sequence(4, 0xC0);
    const uint32_t old_value = LoadU32(atomic_block, 4);

    auto *atomic = atomic_bypass_starvation_bench_.core_.SendAtomic(
        atomic_address, U32Bytes(3), simtix::AtomicExtension::Op::kAdd);
    auto *bypass =
        atomic_bypass_starvation_bench_.core_.SendRead(bypass_address, 4);

    Expect(atomic_bypass_starvation_bench_.WaitForMemoryRequests(1, 32),
           "linearized atomic miss reaches memory even with a younger "
           "non-cacheable request queued behind it");
    ExpectReadRequest(atomic_bypass_starvation_bench_, 0, 0x20, 16,
                      "atomic miss refill read issues before the younger "
                      "non-cacheable bypass");
    Expect(
        !atomic_bypass_starvation_bench_.memory_.RequestHasAtomicExtension(0),
        "linearized atomic miss uses a cache-owned refill read");

    for (size_t i = 0; i < 8; ++i) {
      atomic_bypass_starvation_bench_.AdvanceCycle();
    }
    Expect(atomic_bypass_starvation_bench_.memory_.request_count() == 1,
           "younger non-cacheable bypass stays blocked while the linearized "
           "atomic miss is unresolved");

    atomic_bypass_starvation_bench_.memory_.RespondAt(0, atomic_block);
    Expect(atomic_bypass_starvation_bench_.WaitForCoreResponses(1, 32),
           "linearized atomic miss can refill, replay, and respond");
    Expect(atomic_bypass_starvation_bench_.core_.HasResponse(atomic),
           "linearized atomic response is observed before the bypass");
    Expect(PayloadData(atomic) == U32Bytes(old_value),
           "linearized atomic returns the old refilled value");

    Expect(atomic_bypass_starvation_bench_.WaitForMemoryRequests(2, 32),
           "younger non-cacheable bypass issues after the atomic completes");
    ExpectReadRequest(atomic_bypass_starvation_bench_, 1, bypass_address, 4,
                      "younger non-cacheable request bypasses as the original "
                      "read after atomic completion");
    atomic_bypass_starvation_bench_.memory_.RespondAt(1, bypass_data);
    Expect(atomic_bypass_starvation_bench_.WaitForCoreResponses(2, 32),
           "younger non-cacheable response returns to core");
    Expect(atomic_bypass_starvation_bench_.core_.HasResponse(bypass),
           "younger non-cacheable core request receives its response");
    Expect(PayloadData(bypass) == bypass_data,
           "younger non-cacheable response uses memory data directly");
    Expect(!view.CachedLine(bypass_address).has_value(),
           "younger non-cacheable response does not allocate a cache line");
  }

  void TestAtomicDoesNotDrainUnrelatedOlderMissAndBlocksYoungerTraffic() {
    const auto older_block = Sequence(16, 0x20);
    const auto atomic_block = Sequence(16, 0x80);
    const auto younger_block = Sequence(16, 0xC0);

    auto *older = atomic_order_bench_.core_.SendRead(0x00, 4);
    Expect(atomic_order_bench_.WaitForMemoryRequests(1),
           "older miss reaches memory first");
    ExpectReadRequest(atomic_order_bench_, 0, 0x00, 16,
                      "older miss issues the first refill read");

    auto *atomic = atomic_order_bench_.core_.SendAtomic(
        0x10, U32Bytes(1), simtix::AtomicExtension::Op::kAdd);
    Expect(atomic_order_bench_.WaitForMemoryRequests(2, 32),
           "atomic miss issues without draining unrelated older miss");
    ExpectReadRequest(atomic_order_bench_, 1, 0x10, 16,
                      "atomic miss issues before the younger read");

    auto *younger = atomic_order_bench_.core_.SendRead(0x20, 4);
    for (size_t i = 0; i < 8; ++i) {
      atomic_order_bench_.AdvanceCycle();
    }
    Expect(atomic_order_bench_.memory_.request_count() == 2,
           "younger read stays blocked while atomic miss is unresolved");

    atomic_order_bench_.memory_.RespondAt(1, atomic_block);
    Expect(atomic_order_bench_.WaitForCoreResponses(1, 32),
           "atomic response can return before unrelated older miss");
    Expect(atomic_order_bench_.core_.HasResponse(atomic),
           "atomic ordering response is observed");
    Expect(atomic_order_bench_.WaitForMemoryRequests(3, 32),
           "younger read issues after atomic response");
    ExpectReadRequest(atomic_order_bench_, 2, 0x20, 16,
                      "younger read is the third memory request");

    atomic_order_bench_.memory_.RespondAt(0, older_block);
    atomic_order_bench_.memory_.RespondAt(2, younger_block);
    Expect(atomic_order_bench_.WaitForCoreResponses(3, 32),
           "older and younger reads can still complete");
    Expect(atomic_order_bench_.core_.HasResponse(older),
           "older miss response is observed");
    Expect(atomic_order_bench_.core_.HasResponse(younger),
           "younger miss response is observed");
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

  void TestLockedVictimRefillStallsAndRecovers() {
    CacheModuleTester view(atomic_locked_refill_bench_.cache_);
    const uint64_t locked_address = 0x00;
    const uint64_t replacement_address = 0x40;
    const auto locked_block = Sequence(16, 0x50);
    const auto replacement_block = Sequence(16, 0xA0);

    FillLineWithRead(atomic_locked_refill_bench_, locked_address, locked_block);

    auto *replacement =
        atomic_locked_refill_bench_.core_.SendRead(replacement_address, 4);
    Expect(atomic_locked_refill_bench_.WaitForMemoryRequests(2),
           "replacement miss reaches memory before the atomic lock");

    const size_t response_count =
        atomic_locked_refill_bench_.core_.response_count();
    auto *atomic = atomic_locked_refill_bench_.core_.SendAtomic(
        0x04, U32Bytes(1), simtix::AtomicExtension::Op::kAdd);

    atomic_locked_refill_bench_.AdvanceCycle();
    atomic_locked_refill_bench_.AdvanceCycle();
    atomic_locked_refill_bench_.memory_.RespondAt(1, replacement_block);

    bool saw_stalled_refill = false;
    for (size_t cycle = 0; cycle < 8; ++cycle) {
      atomic_locked_refill_bench_.AdvanceCycle();
      auto line = view.CachedLine(locked_address);
      const auto queues = view.Queues();
      if (line.has_value() && line->tag.locked && queues.mem_resp == 1) {
        saw_stalled_refill = true;
        break;
      }
    }
    Expect(saw_stalled_refill,
           "replacement refill stalls while its only victim is locked");
    auto line = view.CachedLine(locked_address);
    Expect(line.has_value(),
           "locked victim remains installed while refill stalls");
    if (line.has_value()) {
      Expect(line->tag.locked,
             "victim remains locked during the stalled refill");
    }

    Expect(atomic_locked_refill_bench_.WaitForCoreResponses(response_count + 1,
                                                            32),
           "atomic response unlocks the victim");
    Expect(atomic_locked_refill_bench_.core_.HasResponse(atomic),
           "locked-victim atomic response is observed");
    Expect(atomic_locked_refill_bench_.WaitForCoreResponses(response_count + 2,
                                                            32),
           "replacement refill recovers after the atomic unlock");
    Expect(atomic_locked_refill_bench_.core_.HasResponse(replacement),
           "replacement response is observed after locked refill recovers");
    Expect(!view.CachedLine(locked_address).has_value(),
           "locked victim is evicted after refill recovery");
    Expect(view.CachedLine(replacement_address).has_value(),
           "replacement line installs after the lock is released");
  }

  CacheBench atomic_bypass_bench_;
  CacheBench atomic_peer_bench_;
  CacheBench atomic_hit_bench_;
  CacheBench atomic_miss_bench_;
  CacheBench atomic_bypass_starvation_bench_;
  CacheBench atomic_order_bench_;
  CacheBench atomic_escape_hazard_bench_;
  CacheBench atomic_locked_refill_bench_;
};

}  // namespace

int sc_main(int, char *[]) {
  CacheModuleAtomicTestRunner tester("tester");
  sc_core::sc_start();
  return tester.failed() ? 1 : 0;
}
