/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/common/tlm_sink.h>
#include <liblv/interfaces/warp_ctrl.h>
#include <liblv/schema.h>
#include <liblv/trace.h>
#include <systemc.h>
#include <tlm.h>
#include <tlm_core/tlm_1/tlm_req_rsp/tlm_channels/tlm_fifo/tlm_fifo.h>
#include <tlm_utils/simple_target_socket.h>

#include <ostream>
#include <utility>
#include <vector>

namespace lv {
namespace formosa {

class WGInitializer : public sc_module {
 public:
  struct Param {
    unsigned wg_resident_limit = 3;
    unsigned threads_per_warp = 32;
    unsigned warps_per_core = 32;
    unsigned sink_fifo_size = 1;
    unsigned fifo_size = 4;
    bool enable_trace = false;

    std::optional<sol::safe_function> on_wg_dispatch;
    std::optional<sol::safe_function> on_wg_retire;

    // clang-format off
    LV_SCHEMA(WGInitializer, Param,
              LV_FIELD(wg_resident_limit, "Maximum number of resident work-groups"),
              LV_FIELD(threads_per_warp, "Number of threads per warp"),
              LV_FIELD(warps_per_core, "Number of warps per core"),
              LV_FIELD(sink_fifo_size, "Sink FIFO size"),
              LV_FIELD(fifo_size, "FIFO size"),
              LV_FIELD(enable_trace, "Enable perfetto trace or not"),
              LV_FIELD(on_wg_dispatch, "Hook when groups are dispatched"),
              LV_FIELD(on_wg_retire, "Hook when groups are done"))
    // clang-format on
  };

  explicit WGInitializer(const sc_module_name &name, const Param &param);
  ~WGInitializer();

  auto port() const { return &sink_.port; }

  void set_core(WarpCtrl *core);
  WarpCtrl *core() const { return core_; }

 private:
  const uint32_t kWgResidentLimit;
  const uint32_t kThreadsPerWarp;
  const uint32_t kWarpsPerCore;

  TlmSink sink_;

  WarpCtrl *core_;

  uint64_t fifo_size_;

  struct WGDispatchInfo {
    uint64_t kernel_pc;
    uint64_t info_ptr;
    uint32_t group_size;
    explicit WGDispatchInfo(uint64_t pc = 0, uint64_t ptr = 0,
                            uint32_t size = 0)
        : kernel_pc(pc), info_ptr(ptr), group_size(size) {}
    friend ostream &operator<<(ostream &os, const WGDispatchInfo &info) {
      os << "WGDispatchInfo(kernel_pc=" << info.kernel_pc
         << ", info_ptr=" << info.info_ptr << ", group_size=" << info.group_size
         << ")";
      return os;
    }
  };
  sc_fifo<WGDispatchInfo> dispatch_fifo_;
  tlm::tlm_fifo<WarpCtrlCommand> warp_cmd_fifo_;

  enum WGStatus : uint32_t {
    kWGOkay = 0,
    kWGException,
    kWGInvalid,
    kWGDispatchFailed
  };
  struct WGDequeueInfo {
    uint64_t kernel_pc;
    uint64_t info_ptr;
    uint32_t status;
    uint32_t mcause;
    uint64_t mepc;
    uint64_t mtval;

    WGDequeueInfo(uint64_t pc = 0, uint64_t ptr = 0, uint32_t stat = kWGOkay,
                  uint32_t cause = 0, uint64_t epc = 0, uint64_t tval = 0)
        : kernel_pc(pc),
          info_ptr(ptr),
          status(stat),
          mcause(cause),
          mepc(epc),
          mtval(tval) {}
    friend ostream &operator<<(ostream &os, const WGDequeueInfo &info) {
      os << "WGDequeueInfo(kernel_pc=" << info.kernel_pc
         << ", info_ptr=" << info.info_ptr << ", status=" << info.status
         << ", mcause=" << info.mcause << ", mepc=" << info.mepc
         << ", mtval=" << info.mtval << ")";
      return os;
    }
  };
  sc_fifo<WGDequeueInfo> dequeue_fifo_;

  struct CSR {
    // Kernel launch
    uint64_t enq_valid = 0;
    uint64_t enq_kernel_pc = 0;
    uint64_t enq_info_ptr = 0;
    uint64_t enq_wg_size = 0;
    uint64_t deq_valid = 0;
    uint64_t deq_status = 0;
    uint64_t deq_kernel_pc = 0;
    uint64_t deq_info_ptr = 0;
    uint64_t deq_mcause = 0;
    uint64_t deq_mepc = 0;
    uint64_t deq_mtval = 0;
    // Hardware info
    uint64_t wg_resident_limit = 0;
  } csr_;

  struct ProcessingWGInfo {
    bool busy = false;
    uint64_t kernel_pc;
    uint64_t info_ptr;
    uint64_t group_size;
    uint64_t warp_count;
    uint64_t activated_warps;
    uint64_t completed_warps;
  };
  std::vector<ProcessingWGInfo> processing_wg_info_;
  uint32_t processing_wg_count_ = 0;
  std::vector<int> processing_wg_ids_;  // For each warp, the index of the
                                        // processing WG it belongs to
  std::vector<int> processing_wg_order_;

  int FindVacantProcessingWG() const {
    auto vacant_processing_wg =
        std::find_if(processing_wg_info_.begin(), processing_wg_info_.end(),
                     [](const ProcessingWGInfo &wg_info) {
                       return !wg_info.busy;
                     });
    if (vacant_processing_wg != processing_wg_info_.end()) {
      return std::distance(processing_wg_info_.begin(), vacant_processing_wg);
    }
    return -1;  // No vacant processing WG found
  }

  void ProcessRequestThread();
  unsigned int ProcessRequest(tlm::tlm_generic_payload *trans);
  void EnqueueWG();
  void DequeueWG();
  void ProcessThread();

 private:
  sc_dt::sc_bv_base cwm_;
  sc_dt::sc_bv_base release_cwm_;
  std::vector<uint32_t> completed_warps_scratch_;
  std::vector<int> wg_scratch_;
  std::vector<std::pair<int, WGDequeueInfo>> retire_infos_scratch_;
  std::vector<lv::trace::Track> pf_packets_;
  sc_event enqueue_event_, dequeue_event_;

  std::optional<sol::safe_function> on_wg_dispatch_;
  std::optional<sol::safe_function> on_wg_retire_;

  void HandleExceptionWarps();
  void HandleBarrierWarps();
  void DispatchRemainingWarps();
  void DispatchNewWG();
  void EmitWarpCtrlCommand(const WarpCtrlCommand &cmd);
  void TraceWGSlotActiveBegin(int slot_idx);
  void TraceWGSlotInstant(int slot_idx, std::string_view name);
};

}  // namespace formosa
}  // namespace lv
