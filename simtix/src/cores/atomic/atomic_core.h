/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/common/ip_extension.h>
#include <liblv/common/tlm_source.h>
#include <liblv/interfaces/warp_ctrl.h>
#include <liblv/mm/static.h>
#include <liblv/statistics.h>
#include <systemc.h>

#include <memory>

#include "cores/base.h"
#include "cores/encoding.h"
#include "cores/exec_flag.h"
#include "cores/sched/sched.h"
#include "tlm_extensions/atomic_extension.h"

namespace simtix {

class AtomicCore : public BaseCore {
 public:
  AtomicCore(const sc_module_name &name, const ArchParam &p)
      : BaseCore(name, p),
        tmask_(false, num_lanes_),
        wmask_(false, num_warps_),
        pending_barrier_tmask_(
            num_warps_, sc_bv_base{false, static_cast<int>((num_lanes_))}),
        pending_ecall_tmask_(num_warps_,
                             sc_bv_base{false, static_cast<int>((num_lanes_))}),
        mem_port_("mem_port"),
        csr_buf_(num_lanes_),
        addr_buf_(num_lanes_),
        data_buf_(num_lanes_),
        fflags_buf_(num_lanes_),
        trans_(num_lanes_),
        stats_(name, p) {
    exts_.reserve(num_lanes_);
    for (auto &trans : trans_) {
      trans.set_mm(lv::mm::Static);
      exts_.emplace_back(std::make_unique<lv::IpExtension>());
    }
    atomic_exts_.reserve(num_lanes_);
    for (uint32_t i = 0; i < num_lanes_; ++i) {
      atomic_exts_.emplace_back(new AtomicExtension);
    }
    SC_THREAD(MainProc);
    SC_THREAD(IssueMemReq);
    SC_THREAD(CollectMemResp);
  }

  ~AtomicCore() override {
    for (auto &payload : trans_) {
      payload.clear_extension<AtomicExtension>();
    }
  }

  // Lua bindings
  void sched_init(sol::function sched_init) {
    sched_init_ = std::move(sched_init);
  }

  void set_target(lv::TlmSource::Target *target) {
    mem_port_.set_target(target);
  }

  lv::formosa::WarpCtrl *warp_ctrl() {
    return static_cast<lv::formosa::WarpCtrl *>(this);
  }

  lv::TlmSource::Target *target() const { return mem_port_.target(); }
  lv::stats::Group *stats() const { return &stats_; }

  void before_end_of_elaboration() override {
    sched_ = sched_init_("sched");
    sched_init_ = sol::lua_nil;
  }

 private:
  void Activate(const sc_dt::sc_bv_base &cwm, uint64_t pc, uint64_t wg_info,
                uint64_t cwid_base);
  void Resume(const sc_dt::sc_bv_base &cwm);
  void Release(const sc_dt::sc_bv_base &cwm);
  void Abort(const sc_dt::sc_bv_base &cwm);

  uint64_t ArbitratePC(uint32_t wid);
  void UpdateThreadMask(uint32_t wid, uint64_t wpc);
  uint32_t Fetch(uint64_t pc);

  void SetupTrans(uint32_t lane_id, tlm::tlm_command command, uint32_t len);
  void SetupTrans(tlm::tlm_command command, uint32_t len);
  void SetupAtomicTrans(uint32_t lane_id, uint32_t len, AtomicExtension::Op op);
  void SetupAtomicTrans(uint32_t len, AtomicExtension::Op op);
  void ExecuteAtomicInstr(uint32_t len, bool is_signed, AtomicExtension::Op op);

  // Read/write regfiles
  int64_t *ReadRegfile(uint32_t wid, uint8_t rs);
  void WriteRegfile(uint32_t wid, uint8_t rd);

  template <typename RetT, typename CastT>
  void SignExtensionImpl() {
    for (uint32_t i = 0; i < num_lanes_; ++i) {
      if (tmask_[i] == 1) {
        data_buf_[i] =
            static_cast<RetT>(*reinterpret_cast<CastT *>(&data_buf_[i]));
      }
    }
  }

  template <typename T, typename... Rest>
  void DispatchSignExtension(uint32_t size, bool is_signed) {
    if (sizeof(T) == size) {
      if (is_signed) {
        SignExtensionImpl<int64_t, std::make_signed_t<T>>();
      } else {
        SignExtensionImpl<uint64_t, std::make_unsigned_t<T>>();
      }
    } else if constexpr (sizeof...(Rest) > 0) {
      DispatchSignExtension<Rest...>(size, is_signed);
    }
  }

  void SignExtension(uint32_t size, bool is_signed);

  // Update PC
  void AdvancePC(uint32_t wid);
  void UpdatePC(uint32_t wid);

  // Update architectural state based on the output sinks
  void UpdatePri(uint32_t wid, ExecFlag op);
  void UpdateFflags(uint32_t wid);
  void ExecuteCsrOp(uint32_t wid, ExecFlag op);

  // SC Processes
  void MainProc();
  void IssueMemReq();
  void CollectMemResp();
  void ExecuteWarpCtrlCommand(const lv::formosa::WarpCtrlCommand &cmd) override;

  std::shared_ptr<WarpSched> sched_;
  sol::function sched_init_;

  sc_event start_issuing_mem_req_;
  sc_event start_collecting_mem_resp_;
  sc_event done_issuing_mem_req_;
  sc_event done_collecting_mem_resp_;

  sc_bv_base tmask_;
  sc_bv_base wmask_;
  std::vector<sc_bv_base> pending_barrier_tmask_;
  std::vector<sc_bv_base> pending_ecall_tmask_;
  sc_signal<bool> cmd_ready_;
  uint64_t current_pc_ = 0;

  lv::TlmSource mem_port_;
  std::vector<std::unique_ptr<AtomicExtension>> atomic_exts_;
  std::vector<std::unique_ptr<lv::IpExtension>> exts_;
  std::vector<tlm::tlm_generic_payload> trans_;

  // Output sinks
  uint32_t csr_buf_;
  std::vector<uint64_t> addr_buf_;
  std::vector<int64_t> data_buf_;
  std::vector<uint8_t> fflags_buf_;
  uint8_t pri_buf_;

  // Stats
  struct Stats : public BaseCore::Stats {
    Stats(const char *name, const ArchParam &param)
        : BaseCore::Stats(name, param) {}
  } mutable stats_;
};

}  // namespace simtix
