// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>
#include <liblv/interfaces/warp_ctrl.h>
#include <sysc/datatypes/bit/sc_bv_base.h>
#include <systemc.h>

#include <iostream>
#include <memory>
#include <sol/string_view.hpp>
#include <vector>

namespace lv {
namespace formosa {

class StubWarpCtrl : public sc_module, public WarpCtrl {
 public:
  enum class Scenario {
    kAllEcall,
    kAllBarrier,
    kMixedError,
    kMultipleDispatch,
    kCannotActivate,
    kCannotRelease,
    kCannotResume
  };

  StubWarpCtrl(const sc_module_name &name, uint32_t warps_per_core)
      : sc_module(name),
        warps_per_core_(warps_per_core),
        idle_mask_(true, static_cast<int>(warps_per_core)),
        active_mask_(false, static_cast<int>(warps_per_core)),
        barrier_mask_(false, static_cast<int>(warps_per_core)),
        exception_mask_(false, static_cast<int>(warps_per_core)),
        mcause_(warps_per_core, 0),
        mepc_(warps_per_core, 0),
        mtval_(warps_per_core, 0) {
    /* Masks are now initialized in the constructor's initializer list */

    SC_THREAD(Update);
    sensitive << clock_i_;
    SC_THREAD(WarpCtrlCommandThread);
  }

  void SetScenario(sol::string_view scenario_str) {
    cmd_ready_ = true;
    if (scenario_str == "AllBarrier") {
      scenario_ = formosa::StubWarpCtrl::Scenario::kAllBarrier;
    } else if (scenario_str == "MixedError") {
      scenario_ = formosa::StubWarpCtrl::Scenario::kMixedError;
    } else if (scenario_str == "MultipleDispatch") {
      scenario_ = formosa::StubWarpCtrl::Scenario::kMultipleDispatch;
    } else if (scenario_str == "CannotActivate") {
      cmd_ready_ = false;
      scenario_ = formosa::StubWarpCtrl::Scenario::kCannotActivate;
    } else if (scenario_str == "CannotRelease") {
      cmd_ready_ = false;
      scenario_ = formosa::StubWarpCtrl::Scenario::kCannotRelease;
    } else if (scenario_str == "CannotResume") {
      cmd_ready_ = false;
      scenario_ = formosa::StubWarpCtrl::Scenario::kCannotResume;
    } else {
      scenario_ = formosa::StubWarpCtrl::Scenario::kAllEcall;
    }
  }

  void CompleteActiveWarps(sc_dt::sc_bv_base *next_active_mask) {
    wait(sc_time(100, SC_NS));  // Simulate some delay
    for (uint32_t i = 0; i < warps_per_core_; ++i) {
      if (active_mask_[i].to_bool() == true) {
        (*next_active_mask)[i] = 0;
        exception_mask_[i] = 1;
        mcause_[i] = 11;  // Ecall
        exception_mask_changed_event_.notify();
      }
    }
  }

  void BarrierActiveWarps(sc_dt::sc_bv_base *next_active_mask) {
    wait(sc_time(100, SC_NS));  // Simulate some delay
    for (uint32_t i = 0; i < warps_per_core_; ++i) {
      if (active_mask_[i].to_bool() == true) {
        (*next_active_mask)[i] = 0;
        barrier_mask_[i] = 1;
        barrier_mask_changed_event_.notify();
      }
    }
  }

  void ExceptArbitraryWarp(sc_dt::sc_bv_base *next_active_mask) {
    bool error_warp = true;
    for (uint32_t i = 0; i < warps_per_core_; ++i) {
      if (active_mask_[i].to_bool() == true) {
        (*next_active_mask)[i] = 0;
        if (error_warp) {
          exception_mask_[i] = 1;
          mcause_[i] = 2;          // Illegal instruction
          mepc_[i] = 0x1000 + i;   // Example address
          mtval_[i] = 0x2000 + i;  // Example value
          error_warp = false;
          exception_mask_changed_event_.notify();
        } else {
          barrier_mask_[i] = 1;
          barrier_mask_changed_event_.notify();
        }
      }
    }
  }

  void Update() {
    while (true) {
      wait();
      // Assert ready after some delays for these sceneraios
      if ((scenario_ == Scenario::kCannotActivate ||
           scenario_ == Scenario::kCannotRelease ||
           scenario_ == Scenario::kCannotResume) &&
          cmd_ready_ == false) {
        wait(100, SC_NS);
        cmd_ready_ = true;
      }

      // If there are no active warps, do nothing
      if (active_mask_ == 0) {
        continue;
      }

      sc_dt::sc_bv_base next_active_mask = active_mask_;
      if (scenario_ == Scenario::kAllEcall ||
          scenario_ == Scenario::kMultipleDispatch ||
          scenario_ == Scenario::kCannotActivate ||
          scenario_ == Scenario::kCannotRelease) {
        CompleteActiveWarps(&next_active_mask);
      } else if (scenario_ == Scenario::kAllBarrier ||
                 scenario_ == Scenario::kCannotResume) {
        if (!has_barrier_before_) {
          BarrierActiveWarps(&next_active_mask);
        } else {
          // If a barrier was already encountered, complete the warps
          CompleteActiveWarps(&next_active_mask);
        }
        has_barrier_before_ = !has_barrier_before_;
      } else if (scenario_ == Scenario::kMixedError) {
        ExceptArbitraryWarp(&next_active_mask);
      }
      if (active_mask_ != next_active_mask) {
        active_mask_ = next_active_mask;
        active_mask_changed_event_.notify();
      }
    }
  }

  const sc_dt::sc_bv_base &idle_mask() override { return idle_mask_; }
  const sc_dt::sc_bv_base &active_mask() override { return active_mask_; }
  const sc_dt::sc_bv_base &barrier_mask() override { return barrier_mask_; }
  const sc_dt::sc_bv_base &exception_mask() override { return exception_mask_; }

  const sc_core::sc_event &idle_mask_changed_event() override {
    return idle_mask_changed_event_;
  }
  const sc_core::sc_event &active_mask_changed_event() override {
    return active_mask_changed_event_;
  }
  const sc_core::sc_event &barrier_mask_changed_event() override {
    return barrier_mask_changed_event_;
  }
  const sc_core::sc_event &exception_mask_changed_event() override {
    return exception_mask_changed_event_;
  }

  const std::vector<uint64_t> &mcause() override { return mcause_; }
  const std::vector<uint64_t> &mepc() override { return mepc_; }
  const std::vector<uint64_t> &mtval() override { return mtval_; }

  void set_clock(sc_clock *clock) {
    clock_ = clock;
    clock_i_.bind(*clock_);
  }

  sc_clock *clock() const { return clock_; }

 private:
  void Activate(const sc_dt::sc_bv_base &cwm, uint64_t pc, uint64_t wg_info,
                uint64_t cwid_base) {
    std::cout << "StubWarpCtrl::Activate called with:" << std::endl;
    std::cout << "  cwm: " << cwm << std::endl;
    std::cout << "  pc: " << pc << std::endl;
    std::cout << "  wg_info: " << wg_info << std::endl;
    std::cout << "  cwid_base: " << cwid_base << std::endl;

    for (int i = 0; i < cwm.length(); ++i) {
      if (cwm[i].to_bool() == true) {
        idle_mask_[i] = 0;
        active_mask_[i] = 1;
      }
    }
    idle_mask_changed_event_.notify();
    active_mask_changed_event_.notify();
  }

  void Resume(const sc_dt::sc_bv_base &cwm) {
    std::cout << "StubWarpCtrl::Resume called with:" << std::endl;
    std::cout << "  cwm: " << cwm << std::endl;

    for (int i = 0; i < cwm.length(); ++i) {
      if (cwm[i].to_bool() == true) {
        barrier_mask_[i] = 0;
        active_mask_[i] = 1;
      }
    }
    barrier_mask_changed_event_.notify();
    active_mask_changed_event_.notify();
  }

  void Release(const sc_dt::sc_bv_base &cwm) {
    std::cout << "StubWarpCtrl::Release called with:" << std::endl;
    std::cout << "  cwm: " << cwm << std::endl;

    for (int i = 0; i < cwm.length(); ++i) {
      if (cwm[i].to_bool() == true) {
        exception_mask_[i] = 0;
        idle_mask_[i] = 1;
      }
    }
    exception_mask_changed_event_.notify();
    idle_mask_changed_event_.notify();
  }

  void Abort(const sc_dt::sc_bv_base &cwm) {
    std::cout << "StubWarpCtrl::Abort called with:" << std::endl;
    std::cout << "  cwm: " << cwm << std::endl;

    for (int i = 0; i < cwm.length(); ++i) {
      if (cwm[i].to_bool() == true) {
        active_mask_[i] = 0;
        barrier_mask_[i] = 0;
        idle_mask_[i] = 1;
      }
    }
    active_mask_changed_event_.notify();
    barrier_mask_changed_event_.notify();
    idle_mask_changed_event_.notify();
  }

  void WarpCtrlCommandThread() {
    wait(SC_ZERO_TIME);
    if (warp_cmd.size() == 0) {
      return;
    }
    while (true) {
      const auto cmd = warp_cmd->get();
      while (!cmd_ready_) {
        wait(cmd_ready_.posedge_event());
      }
      switch (cmd.op()) {
        case WarpCtrlCommand::Op::kActivate:
          Activate(cmd.cwm(), cmd.pc(), cmd.wg_info(), cmd.cwid_base());
          break;
        case WarpCtrlCommand::Op::kResume:
          Resume(cmd.cwm());
          break;
        case WarpCtrlCommand::Op::kRelease:
          Release(cmd.cwm());
          break;
        case WarpCtrlCommand::Op::kAbort:
          Abort(cmd.cwm());
          break;
      }
    }
  }

  uint32_t warps_per_core_;
  Scenario scenario_;

  bool has_barrier_before_ = false;  // Used to track if a barrier was
                                     // encountered before
  sc_clock *clock_;
  sc_in<bool> clock_i_{"clock_i"};
  sc_signal<bool> cmd_ready_;

  sc_dt::sc_bv_base idle_mask_;
  sc_dt::sc_bv_base active_mask_;
  sc_dt::sc_bv_base barrier_mask_;
  sc_dt::sc_bv_base exception_mask_;
  sc_core::sc_event idle_mask_changed_event_;
  sc_core::sc_event active_mask_changed_event_;
  sc_core::sc_event barrier_mask_changed_event_;
  sc_core::sc_event exception_mask_changed_event_;

  std::vector<uint64_t> mcause_;
  std::vector<uint64_t> mepc_;
  std::vector<uint64_t> mtval_;
};

namespace {

struct Param {
  uint32_t warps_per_core = 32;

  LV_SCHEMA(StubWarpCtrl, Param,
            LV_FIELD(warps_per_core, "Number of warps in the stub core"));
};

}  // namespace

LV_BINDING(formosa, WarpCtrl);

LV_BINDING_WITH_BASES(formosa, StubWarpCtrl, WarpCtrl)
    .constructor(
        [](const char *name, const Param &param) {
          return std::make_shared<StubWarpCtrl>(name, param.warps_per_core);
        },
        lv::params("name", "param"),
        lv::doc("Create a stub warp-control interface."))
    .property("clock", &StubWarpCtrl::clock, &StubWarpCtrl::set_clock,
              lv::doc("SystemC clock."))
    .method("set_scenario", &StubWarpCtrl::SetScenario, lv::params("scenario"),
            lv::doc("Set the stub scenario."));

}  // namespace formosa
}  // namespace lv
