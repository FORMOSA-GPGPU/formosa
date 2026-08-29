// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cache_module_test_common.h"

namespace {

class CacheModuleAtomicTestRunner : public CacheModuleTestRunnerBase {
 public:
  explicit CacheModuleAtomicTestRunner(sc_core::sc_module_name name)
      : CacheModuleTestRunnerBase(name),
        atomic_bypass_bench_("atomic_bypass_bench", MakeAtomicParam(false)),
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
    atomic_hit_bench_.clock_.write(false);
    atomic_miss_bench_.clock_.write(false);
    atomic_bypass_starvation_bench_.clock_.write(false);
    atomic_order_bench_.clock_.write(false);
    atomic_escape_hazard_bench_.clock_.write(false);
    atomic_locked_refill_bench_.clock_.write(false);
    wait(sc_core::SC_ZERO_TIME);

    RunBench("atomic_bypass_bench", [this] {
      TestAtomicBypassesWhenLinearizationDisabled();
    });
    RunBench("atomic_hit_bench", [this] {
      TestAtomicLinearizedHitRmw();
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
