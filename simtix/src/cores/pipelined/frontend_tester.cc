// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>
#include <sysc/kernel/sc_simcontext.h>

#include "cores/pipelined/frontend.h"
#include "konata/konata.h"

#define WITH_TRACER(code) \
  do {                    \
    if (tracer_) {        \
      tracer_->code;      \
    }                     \
  } while (0)

namespace simtix::pipelined {

class FrontendTester : public sc_module, private ToFrontendIntf {
 public:
  FrontendTester(const sc_module_name &name, const ArchParam &param,
                 const Param &pipe_param)
      : sc_module(name),
        active_warps_(false, param.num_warps),
        stats_(name, param),
        frontend("frontend", param, pipe_param, this),
        packet_pool_(param, 10) {
    if (pipe_param.konata_trace_out) {
      try {
        tracer_ = std::make_unique<konata::KonataTracer<Packet>>(
            "tracer", std::string(*pipe_param.konata_trace_out));
      } catch (const std::exception &e) {
        LV_WARNING("Failed to open Konata trace file {}: {}",
                   *pipe_param.konata_trace_out, e.what());
      }
    }
  }

  // Lua bindings
  void activate(uint32_t wid) {
    sc_bv_base bv = active_warps_;
    bv[wid] = 1;
    active_warps_ = bv;
  }

  void deactivate(uint32_t wid) {
    sc_bv_base bv = active_warps_;
    bv[wid] = 0;
    active_warps_ = bv;
  }

  void set_clock(std::shared_ptr<sc_clock> clock) {
    clock_ = clock;
    frontend.clock.bind(*clock);
    WITH_TRACER(clock.bind(*clock));
  }

  Packet *issue(uint32_t wid) {
    Packet *packet = nullptr;
    frontend.to_backend[wid]->nb_get(packet);
    if (packet) {
      WITH_TRACER(AddComment(
          packet, fmt::format("ret = {}\\n", sc_time_stamp().to_string())));
      WITH_TRACER(Retire(packet));
    }
    return packet;
  }

  void redirect(uint32_t wid, uint64_t wpc) {
    frontend.NoteFlush(wid, FlushReason::kJbOthers, wpc, 0);
    frontend.Redirect(wid, wpc);
  }

  void set_target(Frontend::Target *target) { frontend.set_target(target); }

  Stats *stats() override { return &stats_; }
  lv::stats::Group *stats_group() { return &stats_; }

 protected:
  const WarpMask &active_warps() const override { return active_warps_; }
  Packet *AllocatePacket() override { return packet_pool_.Acquire(); }
  void FreePacket(Packet *packet) override { packet_pool_.Release(packet); }
  konata::KonataTracer<Packet> *tracer() override { return tracer_.get(); }

  Stats stats_;

 private:
  std::shared_ptr<sc_clock> clock_;
  Frontend frontend;
  WarpMask active_warps_;
  PacketPool packet_pool_;
  std::unique_ptr<konata::KonataTracer<Packet>> tracer_;
};

#undef WITH_TRACER

LV_BINDING(simtix, FrontendTester)
    .constructor(
        [](const char *name, const ArchParam &param, const Param &pipe_param) {
          return std::make_shared<FrontendTester>(name, param, pipe_param);
        },
        lv::params("name", "param", "pipe_param"),
        lv::doc("Create a frontend tester"))
    .property("clock", &FrontendTester::set_clock, lv::doc("SystemC clock"))
    .method("activate", &FrontendTester::activate, lv::params("wid"),
            lv::doc("Activate one warp"))
    .method("deactivate", &FrontendTester::deactivate, lv::params("wid"),
            lv::doc("Deactivate one warp"))
    .method("issue", &FrontendTester::issue, lv::params("wid"),
            lv::doc("Issue one packet from a warp"))
    .method("redirect", &FrontendTester::redirect, lv::params("wid", "wpc"),
            lv::doc("Redirect a warp program counter"))
    .property("target", &FrontendTester::set_target,
              lv::doc("Instruction memory target"))
    .property("stats", &FrontendTester::stats_group,
              lv::doc("Statistics group"));

}  // namespace simtix::pipelined
