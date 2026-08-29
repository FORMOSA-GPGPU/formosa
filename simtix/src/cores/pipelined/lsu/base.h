/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/common/ip_extension.h>
#include <liblv/common/tlm_source.h>
#include <liblv/mm/static.h>
#include <liblv/statistics.h>
#include <systemc.h>
#include <tlm_core/tlm_1/tlm_req_rsp/tlm_channels/tlm_fifo/tlm_fifo.h>

#include <cassert>
#include <memory>
#include <vector>

#include "cores/pipelined/packet.h"
#include "tlm_extensions/atomic_extension.h"

namespace simtix::pipelined {

class Lsu : public sc_module {
 public:
  using Target = lv::TlmSource::Target;

  sc_in<bool> SC_NAMED(clock);
  sc_port<tlm::tlm_fifo_get_if<Packet *>> SC_NAMED(lsu_req);
  sc_port<tlm::tlm_fifo_put_if<Packet *>> SC_NAMED(lsu_resp);

  explicit Lsu(const sc_module_name &name);
  virtual ~Lsu() = default;
  virtual lv::stats::Group *stats() const { return nullptr; }

  // Lua bindings
  void set_target(lv::TlmSource::Target *target) {
    dmem_port_.set_target(target);
  }

 protected:
  static void SetupAtomicExtensions(
      const sc_bv_base &tmask,
      std::vector<std::unique_ptr<AtomicExtension>> &exts,
      std::vector<tlm::tlm_generic_payload> &trans, AtomicExtension::Op op);
  static void ClearAtomicExtensions(
      std::vector<tlm::tlm_generic_payload> &trans);
  lv::TlmSource dmem_port_;
};

}  // namespace simtix::pipelined
