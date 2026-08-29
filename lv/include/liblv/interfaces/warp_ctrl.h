/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <sysc/datatypes/bit/sc_bv_base.h>
#include <systemc.h>
#include <tlm_core/tlm_1/tlm_req_rsp/tlm_1_interfaces/tlm_core_ifs.h>

#include <cstdint>
#include <vector>

namespace lv {
namespace formosa {

/**
 * Interface for a streaming multi-processor. Will be used by the WGInitializer.
 */
class WarpCtrlCommand {
 public:
  enum class Op : uint8_t { kActivate, kResume, kRelease, kAbort };

  WarpCtrlCommand() = default;

  static WarpCtrlCommand Activate(const sc_dt::sc_bv_base &cwm, uint64_t pc,
                                  uint64_t wg_info, uint64_t cwid_base) {
    return WarpCtrlCommand(Op::kActivate, cwm, pc, wg_info, cwid_base);
  }

  static WarpCtrlCommand Resume(const sc_dt::sc_bv_base &cwm) {
    return WarpCtrlCommand(Op::kResume, cwm);
  }

  static WarpCtrlCommand Release(const sc_dt::sc_bv_base &cwm) {
    return WarpCtrlCommand(Op::kRelease, cwm);
  }

  static WarpCtrlCommand Abort(const sc_dt::sc_bv_base &cwm) {
    return WarpCtrlCommand(Op::kAbort, cwm);
  }

  Op op() const { return op_; }
  const sc_dt::sc_bv_base &cwm() const { return cwm_; }
  uint64_t pc() const { return pc_; }
  uint64_t wg_info() const { return wg_info_; }
  uint64_t cwid_base() const { return cwid_base_; }

 private:
  WarpCtrlCommand(Op op, const sc_dt::sc_bv_base &cwm, uint64_t pc = 0,
                  uint64_t wg_info = 0, uint64_t cwid_base = 0)
      : op_(op), cwm_(cwm), pc_(pc), wg_info_(wg_info), cwid_base_(cwid_base) {}

  Op op_ = Op::kRelease;
  sc_dt::sc_bv_base cwm_;
  uint64_t pc_ = 0;
  uint64_t wg_info_ = 0;
  uint64_t cwid_base_ = 0;
};

class WarpCtrl {
 public:
  virtual ~WarpCtrl() = default;

  sc_core::sc_port<tlm::tlm_get_if<WarpCtrlCommand>, 1,
                   sc_core::SC_ZERO_OR_MORE_BOUND>
      SC_NAMED(warp_cmd);

  // Warp masks to indicate the state of each warp
  virtual const sc_dt::sc_bv_base &idle_mask() = 0;
  virtual const sc_dt::sc_bv_base &active_mask() = 0;
  virtual const sc_dt::sc_bv_base &barrier_mask() = 0;
  virtual const sc_dt::sc_bv_base &exception_mask() = 0;

  virtual const sc_core::sc_event &idle_mask_changed_event() = 0;
  virtual const sc_core::sc_event &active_mask_changed_event() = 0;
  virtual const sc_core::sc_event &barrier_mask_changed_event() = 0;
  virtual const sc_core::sc_event &exception_mask_changed_event() = 0;

  // Exception CSRs
  virtual const std::vector<uint64_t> &mcause() = 0;
  virtual const std::vector<uint64_t> &mepc() = 0;
  virtual const std::vector<uint64_t> &mtval() = 0;
};

}  // namespace formosa
}  // namespace lv
