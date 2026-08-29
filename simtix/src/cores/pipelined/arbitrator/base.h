/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/statistics.h>
#include <systemc.h>
#include <tlm_core/tlm_1/tlm_req_rsp/tlm_1_interfaces/tlm_core_ifs.h>

#include "cores/pipelined/packet.h"
#include "cores/pipelined/scoreboard.h"
#include "konata/konata.h"

namespace simtix::pipelined {

class ToArbitratorIntf {
 public:
  virtual ~ToArbitratorIntf() = default;
  virtual void ReadRegFile(int64_t *ptr, uint32_t wid, uint32_t reg_id) = 0;
  virtual void WriteRegFile(const int64_t *ptr, uint32_t wid, uint32_t reg_id,
                            const sc_bv_base &tmask) = 0;
  virtual konata::KonataTracer<Packet> *tracer() = 0;
};

class ArbitratorIntf : public sc_module {
 public:
  sc_in<bool> SC_NAMED(clock);

  sc_port<tlm::tlm_get_peek_if<Packet *>> SC_NAMED(operand_collect_req);
  sc_port<tlm::tlm_get_peek_if<Packet *>> SC_NAMED(writeback_req);

  sc_port<tlm::tlm_put_if<Packet *>> SC_NAMED(operand_collect_resp);
  sc_port<tlm::tlm_put_if<Packet *>> SC_NAMED(writeback_resp);

  ArbitratorIntf(const sc_module_name &name) : sc_module(name) {}

  virtual ~ArbitratorIntf() = default;

  virtual void set_core(ToArbitratorIntf *core) { core_ = core; }
  virtual void set_scoreboard(Scoreboard *scoreboard) {
    scoreboard_ = scoreboard;
  }

  virtual lv::stats::Group *stats() const { return nullptr; }

 protected:
  ToArbitratorIntf *core_;
  Scoreboard *scoreboard_ = nullptr;
};

}  // namespace simtix::pipelined
