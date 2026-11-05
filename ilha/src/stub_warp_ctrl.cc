// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <ilha/warp_ctrl.h>
#include <liblv/binding.h>
#include <sysc/datatypes/bit/sc_bv_base.h>
#include <systemc.h>

#include <iostream>
#include <memory>
#include <sol/string_view.hpp>
#include <vector>

namespace ilha {

class StubWarpCtrl : public sc_module,
                     public WarpCtrl,
                     public tlm::tlm_put_if<WarpCtrlCommand> {
 public:
  enum class Scenario {
    kAllEcall,
    kStaggeredEcall,
    kAllBarrier,
    kMixedError,
    kSlowAbort,
    kPartialTrap,
    kSlowRelease,
    kStaggeredSlowRelease,
    kRefuseAfterRelease,
    kMultipleDispatch,
    kCannotActivate,
    kCannotRelease,
    kCannotResume,
    kCannotAbort
  };

  StubWarpCtrl(const sc_module_name &name, uint32_t warps_per_core)
      : sc_module(name),
        warps_per_core_(warps_per_core),
        idle_mask_(true, static_cast<int>(warps_per_core)),
        active_mask_(false, static_cast<int>(warps_per_core)),
        barrier_mask_(false, static_cast<int>(warps_per_core)),
        exception_mask_(false, static_cast<int>(warps_per_core)),
        stopping_mask_(false, static_cast<int>(warps_per_core)),
        trap_cwm_(false, static_cast<int>(warps_per_core)),
        stop_at_(warps_per_core),
        mcause_(warps_per_core, 0),
        mepc_(warps_per_core, 0),
        mtval_(warps_per_core, 0),
        barrier_pc_(warps_per_core, 0) {
    /* Masks are now initialized in the constructor's initializer list */

    SC_THREAD(Update);
    sensitive << clock_i_;
    warp_cmd.bind(*this);
    SC_THREAD(CompleteStops);
    SC_THREAD(EndRefusal);
  }

  void SetScenario(sol::string_view scenario_str) {
    cmd_ready_ = true;
    refusing_ = false;
    refusal_end_event_.cancel();
    activations_ = 0;
    trap_cwm_ = 0;
    traps_ = 0;
    if (scenario_str == "StaggeredEcall") {
      scenario_ = StubWarpCtrl::Scenario::kStaggeredEcall;
    } else if (scenario_str == "AllBarrier") {
      scenario_ = StubWarpCtrl::Scenario::kAllBarrier;
    } else if (scenario_str == "MixedError") {
      scenario_ = StubWarpCtrl::Scenario::kMixedError;
    } else if (scenario_str == "SlowAbort") {
      scenario_ = StubWarpCtrl::Scenario::kSlowAbort;
    } else if (scenario_str == "PartialTrap") {
      scenario_ = StubWarpCtrl::Scenario::kPartialTrap;
    } else if (scenario_str == "SlowRelease") {
      scenario_ = StubWarpCtrl::Scenario::kSlowRelease;
    } else if (scenario_str == "StaggeredSlowRelease") {
      scenario_ = StubWarpCtrl::Scenario::kStaggeredSlowRelease;
    } else if (scenario_str == "RefuseAfterRelease") {
      scenario_ = StubWarpCtrl::Scenario::kRefuseAfterRelease;
    } else if (scenario_str == "MultipleDispatch") {
      scenario_ = StubWarpCtrl::Scenario::kMultipleDispatch;
    } else if (scenario_str == "CannotActivate") {
      cmd_ready_ = false;
      scenario_ = StubWarpCtrl::Scenario::kCannotActivate;
    } else if (scenario_str == "CannotRelease") {
      scenario_ = StubWarpCtrl::Scenario::kCannotRelease;
    } else if (scenario_str == "CannotResume") {
      scenario_ = StubWarpCtrl::Scenario::kCannotResume;
    } else if (scenario_str == "CannotAbort") {
      scenario_ = StubWarpCtrl::Scenario::kCannotAbort;
    } else {
      scenario_ = StubWarpCtrl::Scenario::kAllEcall;
    }
  }

  void CompleteActiveWarps(sc_dt::sc_bv_base *next_active_mask) {
    wait(sc_time(100, SC_NS));  // Simulate some delay
    *next_active_mask = active_mask_;
    for (uint32_t i = 0; i < warps_per_core_; ++i) {
      if (active_mask_[i].to_bool() && !stopping_mask_[i].to_bool()) {
        (*next_active_mask)[i] = 0;
        exception_mask_[i] = 1;
        mcause_[i] = 11;  // Ecall
        exception_mask_changed_event_.notify();
        if (scenario_ == Scenario::kStaggeredEcall ||
            scenario_ == Scenario::kStaggeredSlowRelease) {
          break;
        }
      }
    }
  }

  void BarrierActiveWarps(sc_dt::sc_bv_base *next_active_mask) {
    wait(sc_time(100, SC_NS));  // Simulate some delay
    *next_active_mask = active_mask_;
    for (uint32_t i = 0; i < warps_per_core_; ++i) {
      if (active_mask_[i].to_bool() && !stopping_mask_[i].to_bool()) {
        (*next_active_mask)[i] = 0;
        barrier_mask_[i] = 1;
        barrier_mask_changed_event_.notify();
      }
    }
  }

  void TrapWarp(uint32_t wid, uint64_t mcause,
                sc_dt::sc_bv_base *next_active_mask) {
    (*next_active_mask)[wid] = 0;
    exception_mask_[wid] = 1;
    mcause_[wid] = mcause;
    mepc_[wid] = 0x1000 + wid;   // Example address
    mtval_[wid] = 0x2000 + wid;  // Example value
    exception_mask_changed_event_.notify();
  }

  void ExceptArbitraryWarp(sc_dt::sc_bv_base *next_active_mask) {
    bool error_warp = true;
    for (uint32_t i = 0; i < warps_per_core_; ++i) {
      if (active_mask_[i].to_bool() && !stopping_mask_[i].to_bool()) {
        if (error_warp) {
          TrapWarp(i, 2, next_active_mask);  // Illegal instruction
          error_warp = false;
        } else {
          (*next_active_mask)[i] = 0;
          barrier_mask_[i] = 1;
          barrier_mask_changed_event_.notify();
        }
      }
    }
  }

  // Holds every warp until a second Activate, then traps the first warp that
  // Activate placed. The other running warps then exit by ecall.
  void TrapPartialWorkGroup(sc_dt::sc_bv_base *next_active_mask) {
    if (activations_ < 2) {
      return;
    }
    if (trap_cwm_ == 0) {
      CompleteActiveWarps(next_active_mask);
      return;
    }
    wait(sc_time(100, SC_NS));
    *next_active_mask = active_mask_;
    for (uint32_t i = 0; i < warps_per_core_; ++i) {
      if (trap_cwm_[i].to_bool() && active_mask_[i].to_bool() &&
          !stopping_mask_[i].to_bool()) {
        TrapWarp(i, 2, next_active_mask);  // Illegal instruction
        break;
      }
    }
    trap_cwm_ = 0;
  }

  // The last running warp traps, and the stub refuses the commands that
  // follow. The first running warp traps meanwhile, so it must be released
  // rather than aborted, and the work-group must still report the first trap.
  void TrapWhileRefused(sc_dt::sc_bv_base *next_active_mask) {
    if (traps_ > 1 || (traps_ == 1 && !refusing_)) {
      return;
    }
    wait(sc_time(100, SC_NS));
    if (traps_ == 1 && !refusing_) {
      return;
    }
    *next_active_mask = active_mask_;
    bool found = false;
    uint32_t first = 0;
    uint32_t last = 0;
    for (uint32_t i = 0; i < warps_per_core_; ++i) {
      if (active_mask_[i].to_bool() && !stopping_mask_[i].to_bool()) {
        first = found ? first : i;
        last = i;
        found = true;
      }
    }
    if (!found) {
      return;
    }
    if (traps_ == 0) {
      TrapWarp(last, 2, next_active_mask);  // Illegal instruction
      RefuseCommands(sc_time(200, SC_NS));
    } else {
      TrapWarp(first, 6, next_active_mask);  // Misaligned store
    }
    ++traps_;
  }

  void Update() {
    while (true) {
      wait();
      // The core is not ready when the first work-group arrives.
      if (scenario_ == Scenario::kCannotActivate && cmd_ready_ == false) {
        wait(100, SC_NS);
        cmd_ready_ = true;
      }

      // If there are no active warps, do nothing
      if ((active_mask_ & ~stopping_mask_) == 0) {
        continue;
      }

      sc_dt::sc_bv_base next_active_mask = active_mask_;
      if (scenario_ == Scenario::kAllEcall ||
          scenario_ == Scenario::kSlowRelease ||
          scenario_ == Scenario::kStaggeredSlowRelease ||
          scenario_ == Scenario::kRefuseAfterRelease ||
          scenario_ == Scenario::kStaggeredEcall ||
          scenario_ == Scenario::kMultipleDispatch ||
          scenario_ == Scenario::kCannotActivate ||
          scenario_ == Scenario::kCannotRelease) {
        CompleteActiveWarps(&next_active_mask);
        // Refuse the Release of the exited warps.
        if (scenario_ == Scenario::kCannotRelease) {
          RefuseCommands(sc_time(100, SC_NS));
        }
      } else if (scenario_ == Scenario::kAllBarrier ||
                 scenario_ == Scenario::kCannotResume) {
        if (!has_barrier_before_) {
          BarrierActiveWarps(&next_active_mask);
          // Refuse the Resume of the waiting warps.
          if (scenario_ == Scenario::kCannotResume) {
            RefuseCommands(sc_time(100, SC_NS));
          }
        } else {
          // If a barrier was already encountered, complete the warps
          CompleteActiveWarps(&next_active_mask);
        }
        has_barrier_before_ = !has_barrier_before_;
      } else if (scenario_ == Scenario::kMixedError ||
                 scenario_ == Scenario::kSlowAbort) {
        ExceptArbitraryWarp(&next_active_mask);
      } else if (scenario_ == Scenario::kPartialTrap) {
        TrapPartialWorkGroup(&next_active_mask);
      } else if (scenario_ == Scenario::kCannotAbort) {
        TrapWhileRefused(&next_active_mask);
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
  const std::vector<uint64_t> &barrier_pc() override { return barrier_pc_; }

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
    if (scenario_ == Scenario::kPartialTrap && ++activations_ == 2) {
      trap_cwm_ = cwm;
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

    if (scenario_ == Scenario::kSlowRelease ||
        scenario_ == Scenario::kStaggeredSlowRelease ||
        scenario_ == Scenario::kRefuseAfterRelease) {
      StartStops(cwm);
      // Refuse further commands for longer than the released warps take to
      // reach I.
      if (scenario_ == Scenario::kRefuseAfterRelease) {
        RefuseCommands(sc_time(1000, SC_NS));
      }
      return;
    }

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

    sc_dt::sc_bv_base stopping(false, cwm.length());
    for (int i = 0; i < cwm.length(); ++i) {
      if (cwm[i].to_bool() &&
          (active_mask_[i].to_bool() || barrier_mask_[i].to_bool())) {
        // Keep the warps in A/B for a while, as a draining core does.
        if (scenario_ == Scenario::kSlowAbort ||
            scenario_ == Scenario::kPartialTrap) {
          stopping[i] = 1;
        } else {
          active_mask_[i] = 0;
          barrier_mask_[i] = 0;
          idle_mask_[i] = 1;
        }
      }
    }
    active_mask_changed_event_.notify();
    barrier_mask_changed_event_.notify();
    idle_mask_changed_event_.notify();
    StartStops(stopping);
  }

  // Each warp keeps A/B/E for 500 ns from its own start, then becomes idle.
  void StartStops(const sc_dt::sc_bv_base &cwm) {
    const sc_time delay(500, SC_NS);
    bool started = false;
    for (int i = 0; i < cwm.length(); ++i) {
      if (cwm[i].to_bool() && !stopping_mask_[i].to_bool()) {
        stopping_mask_[i] = 1;
        stop_at_[i] = sc_time_stamp() + delay;
        started = true;
      }
    }
    // An earlier pending notification wins; CompleteStops reschedules.
    if (started) {
      stop_event_.notify(delay);
    }
  }

  void CompleteStops() {
    sc_dt::sc_bv_base stopped(false, static_cast<int>(warps_per_core_));
    while (true) {
      wait(stop_event_);
      const sc_time now = sc_time_stamp();
      stopped = 0;
      bool stops_left = false;
      sc_time next_stop_at;
      for (uint32_t i = 0; i < warps_per_core_; ++i) {
        if (!stopping_mask_[i].to_bool()) {
          continue;
        }
        if (stop_at_[i] <= now) {
          stopped[i] = 1;
        } else if (!stops_left || stop_at_[i] < next_stop_at) {
          stops_left = true;
          next_stop_at = stop_at_[i];
        }
      }
      if (stops_left) {
        stop_event_.notify(next_stop_at - now);
      }
      if (stopped == 0) {
        continue;
      }

      active_mask_ &= ~stopped;
      barrier_mask_ &= ~stopped;
      exception_mask_ &= ~stopped;
      idle_mask_ |= stopped;
      stopping_mask_ &= ~stopped;
      idle_mask_changed_event_.notify();
      active_mask_changed_event_.notify();
      barrier_mask_changed_event_.notify();
      exception_mask_changed_event_.notify();
    }
  }

  // Refuses commands at once, rather than from the next delta as lowering
  // cmd_ready_ alone would. cmd_ready_ rises again to notify ok_to_put().
  void RefuseCommands(const sc_time &duration) {
    refusing_ = true;
    cmd_ready_ = false;
    refusal_end_event_.notify(duration);
  }

  void EndRefusal() {
    while (true) {
      wait(refusal_end_event_);
      refusing_ = false;
      cmd_ready_ = true;
    }
  }

  bool ReadyForCommand() const { return !refusing_ && cmd_ready_.read(); }

 public:
  bool nb_put(const WarpCtrlCommand &cmd) override {
    if (!ReadyForCommand()) {
      return false;
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
    return true;
  }

  bool nb_can_put(
      tlm::tlm_tag<WarpCtrlCommand> * /*t*/ = nullptr) const override {
    return ReadyForCommand();
  }

  const sc_core::sc_event &ok_to_put(
      tlm::tlm_tag<WarpCtrlCommand> * /*t*/ = nullptr) const override {
    return cmd_ready_.posedge_event();
  }

  void put(const WarpCtrlCommand &cmd) override {
    while (!nb_put(cmd)) {
      wait(ok_to_put());
    }
  }

 private:
  uint32_t warps_per_core_;
  Scenario scenario_;

  bool has_barrier_before_ = false;  // Used to track if a barrier was
                                     // encountered before
  sc_clock *clock_;
  sc_in<bool> clock_i_{"clock_i"};
  // Written by Update, EndRefusal and, through nb_put, WGI's process.
  sc_signal<bool, SC_MANY_WRITERS> cmd_ready_;
  bool refusing_ = false;

  sc_dt::sc_bv_base idle_mask_;
  sc_dt::sc_bv_base active_mask_;
  sc_dt::sc_bv_base barrier_mask_;
  sc_dt::sc_bv_base exception_mask_;
  // Stopping warps keep A/B/E until their stop_at_; stop_event_ fires at the
  // earliest.
  sc_dt::sc_bv_base stopping_mask_;
  // PartialTrap traps one of the warps its second Activate placed.
  uint32_t activations_ = 0;
  sc_dt::sc_bv_base trap_cwm_;
  // Traps CannotAbort has raised.
  uint32_t traps_ = 0;
  std::vector<sc_core::sc_time> stop_at_;
  sc_core::sc_event stop_event_;
  sc_core::sc_event refusal_end_event_;
  sc_core::sc_event idle_mask_changed_event_;
  sc_core::sc_event active_mask_changed_event_;
  sc_core::sc_event barrier_mask_changed_event_;
  sc_core::sc_event exception_mask_changed_event_;

  std::vector<uint64_t> mcause_;
  std::vector<uint64_t> mepc_;
  std::vector<uint64_t> mtval_;
  std::vector<uint64_t> barrier_pc_;
};

namespace {

struct Param {
  uint32_t warps_per_core = 32;

  LV_SCHEMA(StubWarpCtrl, Param,
            LV_FIELD(warps_per_core, "Number of warps in the stub core"));
};

}  // namespace

LV_BINDING(ilha, WarpCtrl);

LV_BINDING_WITH_BASES(ilha, StubWarpCtrl, WarpCtrl)
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

}  // namespace ilha
