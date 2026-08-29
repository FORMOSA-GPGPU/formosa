// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cache_module_test_common.h"

namespace {

class CacheModuleWatchdogTestRunner : public CacheModuleTestRunnerBase {
 public:
  explicit CacheModuleWatchdogTestRunner(sc_core::sc_module_name name)
      : CacheModuleTestRunnerBase(name),
        watchdog_latency_bench_(
            "watchdog_latency_bench",
            MakeParam(WriteMissPolicy::kWriteAllocate,
                      WriteHitPolicy::kWriteThrough, 1, 2, 1, 2, 1)),
        watchdog_internal_bench_(
            "watchdog_internal_bench",
            MakeParam(WriteMissPolicy::kWriteAllocate,
                      WriteHitPolicy::kWriteThrough, 1, 2, 1, 2, 1)) {
    SC_THREAD(Run);
  }

 private:
  void Run() {
    watchdog_latency_bench_.clock_.write(false);
    watchdog_internal_bench_.clock_.write(false);
    wait(sc_core::SC_ZERO_TIME);

    RunBench("watchdog_latency_bench", [this] {
      TestWatchdogIgnoresMemoryLatency();
    });
    RunBench("watchdog_internal_bench", [this] {
      TestWatchdogTripsOnInternalNoProgress();
    });

    sc_core::sc_stop();
  }

  void TestWatchdogIgnoresMemoryLatency() {
    CacheModuleTester view(watchdog_latency_bench_.cache_);
    view.ArmWatchdog(2);

    auto *read = watchdog_latency_bench_.core_.SendRead(0x00, 4);
    Expect(watchdog_latency_bench_.WaitForMemoryRequests(1),
           "watchdog latency test read miss reaches memory");
    ExpectReadRequest(watchdog_latency_bench_, 0, 0x00, 16,
                      "watchdog latency test miss is block aligned");

    for (size_t cycle = 0; cycle < 8; ++cycle) {
      watchdog_latency_bench_.AdvanceCycle();
    }

    Expect(!view.WatchdogTripped(),
           "watchdog must not trip while the cache is only waiting for memory");
    Expect(view.WatchdogStalledCycles() == 0,
           "memory latency should not accumulate internal stalled cycles");

    watchdog_latency_bench_.memory_.RespondAt(0, Sequence(16, 0x60));
    Expect(watchdog_latency_bench_.WaitForCoreResponses(1, 32),
           "watchdog latency test recovers after memory responds");
    Expect(watchdog_latency_bench_.core_.HasResponse(read),
           "watchdog latency test read response is observed");
  }

  void TestWatchdogTripsOnInternalNoProgress() {
    CacheModuleTester view(watchdog_internal_bench_.cache_);
    view.ArmWatchdog(3);
    view.InjectWatchdogInternalStall(0);

    for (size_t cycle = 0; cycle < 4; ++cycle) {
      watchdog_internal_bench_.AdvanceCycle();
    }

    Expect(view.WatchdogTripped(),
           "watchdog should trip after internal work makes no progress");
    Expect(view.WatchdogStalledCycles() >= 3,
           "watchdog records the internal no-progress duration");
  }

  CacheBench watchdog_latency_bench_;
  CacheBench watchdog_internal_bench_;
};

}  // namespace

int sc_main(int, char *[]) {
  CacheModuleWatchdogTestRunner tester("tester");
  sc_core::sc_start();
  return tester.failed() ? 1 : 0;
}
