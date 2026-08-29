// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cache_module_test_common.h"

namespace {

class CacheModuleMemoryResponseTestRunner : public CacheModuleTestRunnerBase {
 public:
  explicit CacheModuleMemoryResponseTestRunner(sc_core::sc_module_name name)
      : CacheModuleTestRunnerBase(name),
        failed_read_bench_("failed_read_bench", MakeParam()),
        failed_write_bench_("failed_write_bench",
                            MakeParam(WriteMissPolicy::kWriteNoAllocate)),
        failed_victim_write_bench_(
            "failed_victim_write_bench",
            MakeParam(WriteMissPolicy::kWriteAllocate,
                      WriteHitPolicy::kWriteBack, 1, 2, 1, 4)),
        bypass_error_bench_("bypass_error_bench", MakeNonCacheableParam()),
        bypass_incomplete_bench_("bypass_incomplete_bench",
                                 MakeNonCacheableParam()) {
    SC_THREAD(Run);
  }

 private:
  void Run() {
    failed_read_bench_.clock_.write(false);
    failed_write_bench_.clock_.write(false);
    failed_victim_write_bench_.clock_.write(false);
    bypass_error_bench_.clock_.write(false);
    bypass_incomplete_bench_.clock_.write(false);
    wait(sc_core::SC_ZERO_TIME);

    RunBench("failed_read_bench", [this] {
      TestFailedMshrReadIsFatal();
    });
    RunBench("failed_write_bench", [this] {
      TestFailedMemoryWriteIsFatal();
    });
    RunBench("failed_victim_write_bench", [this] {
      TestFailedVictimWriteIsFatal();
    });
    RunBench("bypass_error_bench", [this] {
      TestBypassErrorPropagatesToCore();
    });
    RunBench("bypass_incomplete_bench", [this] {
      TestIncompleteBypassResponseIsFatal();
    });

    sc_core::sc_stop();
  }

  void ExpectResponseFatal(CacheBench &bench, CacheModuleTester &view,
                           size_t request_index,
                           tlm::tlm_response_status status,
                           std::string_view message) {
    bench.memory_.RespondAtWithStatus(request_index, status);
    for (size_t delta = 0; delta < 8 && !view.HasMemoryResponse(); ++delta) {
      wait(sc_core::SC_ZERO_TIME);
    }
    Expect(view.HasMemoryResponse(),
           "failed downstream response reaches the cache response FIFO");

    bool threw = false;
    try {
      view.AcceptMemResponse();
    } catch (const lv::fatal_error &) {
      threw = true;
    }
    Expect(threw, message);
  }

  void TestFailedMshrReadIsFatal() {
    CacheModuleTester view(failed_read_bench_.cache_);
    failed_read_bench_.core_.SendRead(0x00, 4);
    Expect(failed_read_bench_.WaitForMemoryRequests(1),
           "MSHR read reaches memory before the failed response");

    ExpectResponseFatal(failed_read_bench_, view, 0,
                        tlm::TLM_ADDRESS_ERROR_RESPONSE,
                        "failed MSHR read response is fatal");
    Expect(!view.CachedLine(0x00).has_value(),
           "failed MSHR read does not install a cache line");
    Expect(failed_read_bench_.core_.response_count() == 0,
           "failed MSHR read does not return successful data to the core");
    Expect(view.Queues().mem_resp == 0,
           "failed MSHR read does not enter the refill response queue");
    Expect(view.Queues().mem_inflight_packets == 1,
           "fatal validation happens before inflight response retirement");
    Expect(view.Mshr().pending_refill_entries == 1,
           "fatal validation happens before MSHR refill notification");
  }

  void TestFailedMemoryWriteIsFatal() {
    CacheModuleTester view(failed_write_bench_.cache_);
    failed_write_bench_.core_.SendWrite(0x04, Sequence(4, 0x90));
    Expect(failed_write_bench_.WaitForMemoryRequests(1),
           "write-buffer request reaches memory before the failed response");
    Expect(failed_write_bench_.memory_.request(0)->is_write(),
           "failed normal memory response belongs to a write request");

    ExpectResponseFatal(failed_write_bench_, view, 0,
                        tlm::TLM_GENERIC_ERROR_RESPONSE,
                        "failed write-buffer memory response is fatal");
  }

  void TestFailedVictimWriteIsFatal() {
    CacheModuleTester view(failed_victim_write_bench_.cache_);
    const auto old_block = Sequence(16, 0x20);
    const auto replacement_block = Sequence(16, 0x60);
    (void)FillAndDirtyLine(failed_victim_write_bench_, 0x00, old_block, 4,
                           Sequence(4, 0xE0));

    const size_t replacement_index =
        failed_victim_write_bench_.memory_.request_count();
    const size_t response_count =
        failed_victim_write_bench_.core_.response_count();
    failed_victim_write_bench_.core_.SendRead(0x40, 4);
    Expect(failed_victim_write_bench_.WaitForMemoryRequests(
               replacement_index + 1, 32),
           "replacement read reaches memory before victim generation");
    failed_victim_write_bench_.memory_.RespondAt(replacement_index,
                                                 replacement_block);
    Expect(
        failed_victim_write_bench_.WaitForCoreResponses(response_count + 1, 32),
        "replacement refill completes before victim write response");

    const size_t victim_index = replacement_index + 1;
    Expect(
        failed_victim_write_bench_.WaitForMemoryRequests(victim_index + 1, 32),
        "dirty victim write reaches memory before the failed response");
    Expect(failed_victim_write_bench_.memory_.request(victim_index)->is_write(),
           "failed victim response belongs to a write request");

    ExpectResponseFatal(failed_victim_write_bench_, view, victim_index,
                        tlm::TLM_ADDRESS_ERROR_RESPONSE,
                        "failed dirty-victim memory response is fatal");
  }

  void TestBypassErrorPropagatesToCore() {
    CacheModuleTester view(bypass_error_bench_.cache_);
    auto *read = bypass_error_bench_.core_.SendRead(0x20, 4);
    Expect(bypass_error_bench_.WaitForMemoryRequests(1),
           "non-cacheable bypass read reaches memory");
    bypass_error_bench_.memory_.RespondAtWithStatus(
        0, tlm::TLM_ADDRESS_ERROR_RESPONSE);

    Expect(bypass_error_bench_.WaitForCoreResponses(1, 32),
           "explicit bypass error returns to the core");
    Expect(bypass_error_bench_.core_.response(0) == read,
           "bypass error completes the original core request");
    Expect(read->get_response_status() == tlm::TLM_ADDRESS_ERROR_RESPONSE,
           "bypass response preserves the downstream address error");
    Expect(!view.CachedLine(0x20).has_value(),
           "failed bypass request does not install a cache line");
  }

  void TestIncompleteBypassResponseIsFatal() {
    CacheModuleTester view(bypass_incomplete_bench_.cache_);
    bypass_incomplete_bench_.core_.SendRead(0x20, 4);
    Expect(bypass_incomplete_bench_.WaitForMemoryRequests(1),
           "incomplete bypass read reaches memory");

    ExpectResponseFatal(bypass_incomplete_bench_, view, 0,
                        tlm::TLM_INCOMPLETE_RESPONSE,
                        "incomplete bypass response is fatal");
  }

  CacheBench failed_read_bench_;
  CacheBench failed_write_bench_;
  CacheBench failed_victim_write_bench_;
  CacheBench bypass_error_bench_;
  CacheBench bypass_incomplete_bench_;
};

}  // namespace

int sc_main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  CacheModuleMemoryResponseTestRunner runner("runner");
  sc_core::sc_start();
  return runner.failed() ? 1 : 0;
}
