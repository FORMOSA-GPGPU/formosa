// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>
#include <liblv/common/tlm_sink.h>
#include <systemc.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>

#include "cores/pipelined/arbitrator/simple.h"
#include "cores/pipelined/lsu/simple.h"
#include "cores/pipelined/pipelined_core.h"
#include "cores/sched/lrr.h"

namespace {

using ilha::WarpCtrlCommand;
using namespace simtix::pipelined;  // NOLINT

simtix::ArchParam Arch() {
  simtix::ArchParam arch;
  arch.num_warps = 2;
  arch.num_lanes = 1;
  return arch;
}

Param Pipeline(bool ghost) {
  Param param;
  param.num_subcores = 1;
  param.enable_ghost_scheduler = ghost;
  return param;
}

class AbortTest : public sc_module {
 public:
  AbortTest(const sc_module_name &name, bool ghost)
      : sc_module(name),
        clock_("clock", sc_time(1, SC_NS)),
        imem_("imem"),
        dmem_("dmem"),
        core_("core", Arch(), Pipeline(ghost)) {
    core_.clock.bind(clock_);
    core_.set_imem(&imem_.port);
    lua_.set_function("scheduler", []() -> std::shared_ptr<simtix::WarpSched> {
      return std::make_shared<simtix::Lrr>(Arch().num_warps);
    });
    lua_.set_function("lsu", [](const char *name) -> std::shared_ptr<Lsu> {
      return std::make_shared<SimpleLsu>(name, Arch());
    });
    lua_.set_function("arbitrator",
                      [](const char *name) -> std::shared_ptr<ArbitratorIntf> {
                        return std::make_shared<SimpleArbitrator>(name, Arch());
                      });
    auto *backend = core_.subcores().value()[0];
    backend->sched_init(lua_["scheduler"]);
    backend->lsu_init(lua_["lsu"]);
    backend->arbitrator_init(lua_["arbitrator"]);
    backend->set_target(&dmem_.port);
    SC_THREAD(ServeInstructions);
    SC_THREAD(HoldDataResponse);
    SC_THREAD(Run);
    SC_METHOD(CheckStates);
    sensitive << core_.idle_mask_changed_event()
              << core_.active_mask_changed_event()
              << core_.barrier_mask_changed_event()
              << core_.exception_mask_changed_event();
  }

  bool failed() const { return failed_; }

 private:
  static constexpr uint64_t kLoadPC = 0x100;
  static constexpr uint64_t kExitPC = 0x200;
  static constexpr uint64_t kBarrierPC = 0x300;

  void CheckStates() {
    for (unsigned wid = 0; wid < Arch().num_warps; ++wid) {
      const unsigned states = core_.idle_mask()[wid].to_bool() +
                              core_.active_mask()[wid].to_bool() +
                              core_.barrier_mask()[wid].to_bool() +
                              core_.exception_mask()[wid].to_bool();
      Check(states == 1, "warp state must remain one-hot I/A/B/E");
    }
  }

  void Check(bool condition, const char *message) {
    if (!condition) {
      failed_ = true;
      std::cerr << sc_time_stamp() << ": " << message << '\n';
    }
  }

  void Step(unsigned cycles) {
    for (unsigned i = 0; i < cycles; ++i) wait(clock_.posedge_event());
  }

  template <typename Predicate>
  bool Until(Predicate predicate) {
    for (unsigned i = 0; i < 200; ++i) {
      if (predicate()) return true;
      Step(1);
    }
    return predicate();
  }

  static sc_bv_base Mask(unsigned wid) {
    sc_bv_base mask(false, Arch().num_warps);
    mask[wid] = 1;
    return mask;
  }

  void ServeInstructions() {
    while (true) {
      auto *trans = imem_.req_port->read();
      for (unsigned i = 0; i < trans->get_data_length(); ++i) {
        const auto addr = trans->get_address() + i;
        const auto pc = addr & ~uint64_t{3};
        uint32_t word = 0x00000013;                // nop
        if (pc == kLoadPC) word = 0x00003083;      // ld x1, 0(x0)
        if (pc == kLoadPC + 4) word = 0xffffffff;  // illegal
        if (pc == kExitPC) word = 0x00000073;      // ecall
        if (pc == kBarrierPC) word = 0x0000102b;   // fsa.bar 0, 0
        trans->get_data_ptr()[i] = word >> (8 * (addr % 4));
      }
      trans->set_response_status(tlm::TLM_OK_RESPONSE);
      imem_.resp_port->write(trans);
    }
  }

  void HoldDataResponse() {
    pending_ = dmem_.req_port->read();
    Check(pending_->is_read(), "expected an outstanding load");
  }

  void Run() {
    core_.warp_cmd->put(WarpCtrlCommand::Activate(Mask(0), kLoadPC, 0, 0));
    Check(core_.active_mask()[0].to_bool(),
          "Activate returned before applying its control effects");
    if (!Until([this] {
          return pending_ != nullptr;
        })) {
      Check(false, "load did not reach memory");
      sc_stop();
      return;
    }
    // Let the illegal instruction reach the backend, its exception pending
    // behind the withheld load.
    Step(32);
    Check(core_.active_mask()[0].to_bool(), "load must keep warp 0 active");
    core_.warp_cmd->put(WarpCtrlCommand::Abort(Mask(0)));
    Step(4);
    Check(core_.active_mask()[0].to_bool(),
          "aborting warp must retain A until draining completes");
    Check(!core_.idle_mask()[0].to_bool(),
          "aborted warp became idle before its memory response drained");

    // A different warp's command must not wait for warp 0 to drain.
    core_.warp_cmd->put(WarpCtrlCommand::Activate(Mask(1), kExitPC, 0, 0));
    if (!Until([this] {
          return core_.exception_mask()[1].to_bool();
        })) {
      Check(false, "unrelated warp command stalled behind the aborted warp");
      sc_stop();
      return;
    }
    Check(core_.warp_ctrl()->mcause()[1] == 11,
          "unrelated warp must complete by ecall");
    Check(!core_.idle_mask()[0].to_bool(),
          "aborted warp became idle while its load was still withheld");

    std::memset(pending_->get_data_ptr(), 0, pending_->get_data_length());
    pending_->set_response_status(tlm::TLM_OK_RESPONSE);
    dmem_.resp_port->write(pending_);
    pending_ = nullptr;
    Check(Until([this] {
            return core_.idle_mask()[0].to_bool();
          }),
          "aborted warp did not become idle after draining");
    Check(!core_.exception_mask()[0].to_bool(),
          "aborted warp published its old pending exception");
    core_.warp_cmd->put(WarpCtrlCommand::Release(Mask(1)));
    Check(Until([this] {
            return core_.idle_mask()[1].to_bool();
          }),
          "released warp did not become idle");
    core_.warp_cmd->put(WarpCtrlCommand::Activate(Mask(1), kBarrierPC, 0, 0));
    Check(Until([this] {
            return core_.barrier_mask()[1].to_bool();
          }),
          "warp did not reach barrier");
    core_.warp_cmd->put(WarpCtrlCommand::Abort(Mask(1)));
    Check(Until([this] {
            return core_.idle_mask()[1].to_bool();
          }),
          "barrier warp did not become idle after abort");
    core_.warp_cmd->put(WarpCtrlCommand::Activate(Mask(1), kExitPC, 0, 0));
    Check(Until([this] {
            return core_.exception_mask()[1].to_bool();
          }),
          "warp did not reach exception");
    // Abort a warp that is already in E.
    core_.warp_cmd->put(WarpCtrlCommand::Abort(Mask(1)));
    Check(core_.exception_mask()[1].to_bool(),
          "Abort must leave an already trapped warp for Release");
    core_.warp_cmd->put(WarpCtrlCommand::Release(Mask(1)));
    Check(Until([this] {
            return core_.idle_mask()[1].to_bool();
          }),
          "exception warp did not become idle after abort");
    core_.warp_cmd->put(WarpCtrlCommand::Activate(Mask(0), kExitPC, 0, 0));
    Check(Until([this] {
            return core_.exception_mask()[0].to_bool();
          }),
          "drained warp could not be reused");
    Check(core_.warp_ctrl()->mcause()[0] == 11,
          "reused warp inherited an old exception");
    sc_stop();
  }

  sol::state lua_;
  sc_clock clock_;
  lv::TlmSink imem_;
  lv::TlmSink dmem_;
  PipelinedCore core_;
  tlm::tlm_generic_payload *pending_ = nullptr;
  bool failed_ = false;
};

}  // namespace

int sc_main(int argc, char *[]) {
  AbortTest test("test", argc > 1);
  sc_start();
  return test.failed() ? 1 : 0;
}
