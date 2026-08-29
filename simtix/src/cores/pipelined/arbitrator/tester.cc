// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <fmt/format.h>
#include <fmt/ostream.h>
#include <liblv/binding.h>
#include <systemc.h>
#include <tlm_core/tlm_1/tlm_req_rsp/tlm_1_interfaces/tlm_core_ifs.h>
#include <tlm_core/tlm_1/tlm_req_rsp/tlm_channels/tlm_fifo/tlm_fifo.h>

#include <cassert>
#include <cstdlib>
#include <sol/types.hpp>

#include "cores/pipelined/arbitrator/base.h"
#include "cores/pipelined/param.h"
#include "cores/pipelined/scoreboard.h"
#include "fmt/base.h"
#include "konata/konata.h"

#define WITH_TRACER(code) \
  do {                    \
    if (tracer_) {        \
      tracer_->code;      \
    }                     \
  } while (0)

namespace simtix::pipelined {

class ArbitratorTester : public sc_module, private ToArbitratorIntf {
 public:
  sc_in<bool> clk;

  ArbitratorTester(const sc_module_name name, const ArchParam &param,
                   const Param &pipe_param)
      : sc_module(name),
        num_lanes_(param.num_lanes),
        num_warps_(param.num_warps),
        num_threads_(num_lanes_ * num_warps_),
        num_subcores_(pipe_param.num_subcores),
        pool_(param, 10),
        operand_collect_reqs_("operand_collect_req"),
        operand_collect_resps_("operand_collect_resp"),
        writeback_reqs_("writeback_req"),
        writeback_resps_("writeback_resp"),
        committed_counts_(num_subcores_, 0),
        arbitrator_inits_(num_subcores_) {
    if (pipe_param.konata_trace_out) {
      try {
        tracer_ = std::make_unique<konata::KonataTracer<Packet>>(
            "tracer", std::string(*pipe_param.konata_trace_out));
      } catch (const std::exception &e) {
        LV_WARNING("Failed to open Konata trace file {}: {}",
                   *pipe_param.konata_trace_out, e.what());
      }
    }

    operand_collect_reqs_.init(num_subcores_, [](const char *name, size_t id) {
      return new tlm::tlm_fifo<Packet *>(name, 2);
    });
    operand_collect_resps_.init(num_subcores_, [](const char *name, size_t id) {
      return new tlm::tlm_fifo<Packet *>(name, 2);
    });
    writeback_reqs_.init(num_subcores_, [](const char *name, size_t id) {
      return new tlm::tlm_fifo<Packet *>(name, 2);
    });
    writeback_resps_.init(num_subcores_, [](const char *name, size_t id) {
      return new tlm::tlm_fifo<Packet *>(name, 2);
    });

    scoreboards_.reserve(num_subcores_);
    arbitrators_.resize(num_subcores_);
    for (uint32_t i = 0; i < num_subcores_; ++i) {
      scoreboards_.emplace_back(num_warps_ / num_subcores_, num_subcores_);

      sc_spawn(sc_bind(&ArbitratorTester::GenerateOperandCollectReq, this, i));
      sc_spawn(sc_bind(&ArbitratorTester::GenerateWritebackReq, this, i));
      sc_spawn(sc_bind(&ArbitratorTester::Commit, this, i));
    }

    WITH_TRACER(clock.bind(clk));
  }

  // Lua bindings
  void arbitrator_init(uint32_t i, sol::function arbitrator_init) {
    arbitrator_inits_.at(i) = std::move(arbitrator_init);
  }

  void before_end_of_elaboration() override {
    for (uint32_t i = 0; i < num_subcores_; ++i) {
      auto &arb_init = arbitrator_inits_[i];
      auto arb = arb_init(fmt::format("arbitrator{}", i));
      arb_init = sol::lua_nil;

      arbitrators_[i] = arb;
      arbitrators_[i]->clock.bind(clk);
      arbitrators_[i]->set_core(this);
      arbitrators_[i]->set_scoreboard(&scoreboards_[i]);
      arbitrators_[i]->operand_collect_req.bind(operand_collect_reqs_[i]);
      arbitrators_[i]->operand_collect_resp.bind(operand_collect_resps_[i]);
      arbitrators_[i]->writeback_req.bind(writeback_reqs_[i]);
      arbitrators_[i]->writeback_resp.bind(writeback_resps_[i]);
    }
  }

  void test(uint32_t seed, uint32_t count) {
    srand(seed);
    target_count_ = count;
    sc_start();
  }

  void set_clock(std::shared_ptr<sc_clock> clock) {
    clk.bind(*clock);
    clock_ = clock;
  }

 protected:
  void ReadRegFile(int64_t *data, uint32_t wid, uint32_t reg_id) override {
    for (uint32_t i = 0; i < num_lanes_; ++i) {
      data[i] = ToRegfileGoldenData(wid, reg_id, i);
    }
  }

  void WriteRegFile(const int64_t *data, uint32_t wid, uint32_t reg_id,
                    const sc_bv_base &tmask) override {
    for (uint32_t i = 0; i < num_lanes_; ++i) {
      if (tmask[i]) {
        assert(data[i] == ToRegfileGoldenData(wid, reg_id, i));
      }
    }
  }

  konata::KonataTracer<Packet> *tracer() override { return tracer_.get(); }

 private:
  void GenerateOperandCollectReq(uint32_t subcore_id) {
    while (true) {
      /* Randomly generate a request */
      Packet *packet = pool_.Acquire();
      packet->wid =
          subcore_id + (rand() % num_warps_ / num_subcores_) * num_subcores_;

      WITH_TRACER(Declare(packet));
      WITH_TRACER(StartStage(packet, 0, "Sc"));
      wait(clk.posedge_event());

      uint32_t rs1 = rand() % 32;
      uint32_t rs2 = rand() % 32;
      uint32_t rs3 = rand() % 32;
      uint32_t rd = rand() % 32;

      packet->instr.Reset();
      uint32_t iword = (rs3 << 27) | (rs2 << 20) | (rs1 << 15) | (rd << 7);

      /* Randomly choose instruction type to test different register
       * combinations */
      int type = rand() % 4;
      if (type == 0) {
        simtix::RType::Fill(&packet->instr, iword);
        WITH_TRACER(AddMnemonic(
            packet, fmt::format("r{} <- r{}, r{}", packet->instr.rd(),
                                packet->instr.rs1(), packet->instr.rs2())));
      } else if (type == 1) {
        simtix::IType::Fill(&packet->instr, iword);
        WITH_TRACER(
            AddMnemonic(packet, fmt::format("r{} <- r{}", packet->instr.rd(),
                                            packet->instr.rs1())));
      } else if (type == 2) {
        simtix::R4Type::Fill(&packet->instr, iword);
        WITH_TRACER(AddMnemonic(
            packet, fmt::format("r{} <- r{}, r{}, r{}", packet->instr.rd(),
                                packet->instr.rs1(), packet->instr.rs2(),
                                packet->instr.rs3())));
      } else if (type == 3) {
        simtix::UType::Fill(&packet->instr, iword);
        WITH_TRACER(
            AddMnemonic(packet, fmt::format("r{} <- ??", packet->instr.rd())));
      }

      /* Check RAW, WAW, and WAR dependencies */
      while (!scoreboards_[subcore_id].CanIssue(packet)) {
        wait(clk.posedge_event());
      }

      /* Issue the instruction in the scoreboard */
      scoreboards_[subcore_id].Issue(packet);

      /* Ensure data vectors are properly sized */
      packet->rs1_data.resize(num_lanes_);
      packet->rs2_data.resize(num_lanes_);
      packet->rs3_data.resize(num_lanes_);
      packet->data_buf.resize(num_lanes_);

      /* Set random lanes active in tmask */
      for (uint32_t i = 0; i < num_lanes_; ++i) {
        packet->tmask[i] = (rand() % 2 == 0);
      }

      WITH_TRACER(StartStage(packet, 0, "Oc"));
      WITH_TRACER(AddComment(
          packet, fmt::format("tmask = {}", fmt::streamed(packet->tmask))));

      operand_collect_reqs_[subcore_id].put(packet);
    }
  }

  void GenerateWritebackReq(uint32_t subcore_id) {
    while (true) {
      Packet *packet = operand_collect_resps_[subcore_id].get();
      WITH_TRACER(StartStage(packet, 0, "X"));

      /* Check consistency of collected operand data */
      if (packet->instr.rs1() != Instr::kNullReg) {
        for (uint32_t i = 0; i < num_lanes_; ++i) {
          // if (packet->rs1_data[i] !=
          //     ToRegfileGoldenData(packet->wid, packet->instr.rs1(), i)) {
          //   fmt::println("wid = {}, reg_id = {}", packet->wid,
          //                packet->instr.rs1());
          //   fmt::print("rs1 data = {}\n", packet->rs1_data[i]);
          //   fmt::print(
          //       "golden = {}\n",
          //       ToRegfileGoldenData(packet->wid, packet->instr.rs1(), i));
          //   fmt::print("Failed!\n\n");
          // }
          assert(packet->rs1_data[i] ==
                 ToRegfileGoldenData(packet->wid, packet->instr.rs1(), i));
        }
      }
      if (packet->instr.rs2() != Instr::kNullReg) {
        for (uint32_t i = 0; i < num_lanes_; ++i) {
          // if (packet->rs2_data[i] !=
          //     ToRegfileGoldenData(packet->wid, packet->instr.rs2(), i)) {
          //   fmt::println("wid = {}, reg_id = {}", packet->wid,
          //                packet->instr.rs2());
          //   fmt::print("rs2 data = {}\n", packet->rs2_data[i]);
          //   fmt::print(
          //       "golden = {}\n",
          //       ToRegfileGoldenData(packet->wid, packet->instr.rs2(), i));
          //   fmt::print("Failed!\n\n");
          // }
          assert(packet->rs2_data[i] ==
                 ToRegfileGoldenData(packet->wid, packet->instr.rs2(), i));
        }
      }
      if (packet->instr.rs3() != Instr::kNullReg) {
        for (uint32_t i = 0; i < num_lanes_; ++i) {
          // if (packet->rs3_data[i] !=
          //     ToRegfileGoldenData(packet->wid, packet->instr.rs3(), i)) {
          //   fmt::println("wid = {}, reg_id = {}", packet->wid,
          //                packet->instr.rs3());
          //   fmt::println("wid = {}, reg_id = {}", packet->wid,
          //                packet->instr.rs3());
          //   fmt::print("rs3 data = {}\n", packet->rs3_data[i]);
          //   fmt::print(
          //       "golden = {}\n",
          //       ToRegfileGoldenData(packet->wid, packet->instr.rs3(), i));
          //   fmt::print("Failed!\n\n");
          // }
          assert(packet->rs3_data[i] ==
                 ToRegfileGoldenData(packet->wid, packet->instr.rs3(), i));
        }
      }

      /* Random delay between 1 and 5 cycles */
      uint32_t delay = 1 + (rand() % 5);
      for (uint32_t i = 0; i < delay; ++i) {
        wait(clk->posedge_event());
      }

      /* Write golden data to rd_data (data_buf) for writeback */
      for (uint32_t i = 0; i < num_lanes_; ++i) {
        if (packet->instr.rd() != Instr::kNullReg && packet->instr.rd() != 0 &&
            packet->tmask[i]) {
          packet->data_buf[i] =
              ToRegfileGoldenData(packet->wid, packet->instr.rd(), i);
        }
      }

      writeback_reqs_[subcore_id].put(packet);
    }
  }

  void Commit(uint32_t subcore_id) {
    while (true) {
      Packet *packet = writeback_resps_[subcore_id].get();
      WITH_TRACER(StartStage(packet, 0, "Cm"));
      wait(clk->posedge_event());

      /* Check that the data_buf contains golden data at each active lane */
      for (uint32_t i = 0; i < num_lanes_; ++i) {
        if (packet->instr.rd() != Instr::kNullReg && packet->instr.rd() != 0 &&
            packet->tmask[i]) {
          assert(packet->data_buf[i] ==
                 ToRegfileGoldenData(packet->wid, packet->instr.rd(), i));
        }
      }

      WITH_TRACER(Retire(packet));
      scoreboards_[subcore_id].Commit(packet);

      std::fill(packet->rs1_data.begin(), packet->rs1_data.end(), 0);
      std::fill(packet->rs2_data.begin(), packet->rs2_data.end(), 0);
      std::fill(packet->rs3_data.begin(), packet->rs3_data.end(), 0);
      std::fill(packet->data_buf.begin(), packet->data_buf.end(), 0);
      pool_.Release(packet);

      /* Increment committed instruction count for this subcore */
      committed_counts_[subcore_id]++;

      /* Check if all subcores have reached the target count */
      bool all_reached = true;
      for (uint32_t count : committed_counts_) {
        if (count < target_count_) {
          all_reached = false;
          break;
        }
      }

      if (all_reached) {
        sc_pause();
      }
    }
  }

  uint32_t get_subcore_id(uint32_t wid) const { return wid % num_subcores_; }

  int64_t ToRegfileGoldenData(uint32_t wid, uint32_t reg_id, uint32_t lane_id) {
    if (reg_id == 0) {
      return 0;
    }
    uint32_t val = lane_id;
    val += reg_id << log2(num_lanes_);
    val += wid << (log2(num_lanes_) + log2(num_warps_));
    return val;
  }

  static uint32_t log2(uint32_t x) {
    if (x == 0) {
      return 0;  // Example handling
    }
    return (31 - __builtin_clz(x));
  }

  const uint32_t num_lanes_;
  const uint32_t num_warps_;
  const uint32_t num_threads_;
  const uint32_t num_subcores_;

  sc_vector<tlm::tlm_fifo<Packet *>> operand_collect_reqs_;
  sc_vector<tlm::tlm_fifo<Packet *>> operand_collect_resps_;
  sc_vector<tlm::tlm_fifo<Packet *>> writeback_reqs_;
  sc_vector<tlm::tlm_fifo<Packet *>> writeback_resps_;

  std::shared_ptr<sc_clock> clock_;
  std::vector<Scoreboard> scoreboards_;
  std::vector<uint32_t> committed_counts_;
  uint32_t target_count_ = 0;
  std::vector<std::shared_ptr<ArbitratorIntf>> arbitrators_;
  std::vector<sol::function> arbitrator_inits_;

  std::unique_ptr<konata::KonataTracer<Packet>> tracer_;
  PacketPool pool_;
};

#undef WITH_TRACER

LV_BINDING(simtix, ArbitratorTester)
    .constructor(
        [](const char *name, const ArchParam &param, const Param &pipe_param) {
          return std::make_shared<ArbitratorTester>(name, param, pipe_param);
        },
        lv::params("name", "param", "pipe_param"),
        lv::doc("Create an arbitrator tester"))
    .method(
        "arbitrator_init", &ArbitratorTester::arbitrator_init,
        lv::params(lv::param("index"),
                   lv::param("arbitrator_init",
                             lv::lua_type(
                                 "fun(name: string): simtix.ArbitratorIntf"))),
        lv::doc("Register an arbitrator factory for one subcore"))
    .method("test", &ArbitratorTester::test, lv::params("seed", "count"),
            lv::doc("Run the arbitrator test"))
    .property("clock", &ArbitratorTester::set_clock, lv::doc("SystemC clock"));

}  // namespace simtix::pipelined
