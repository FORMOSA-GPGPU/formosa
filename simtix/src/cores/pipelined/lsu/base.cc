// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/pipelined/lsu/base.h"

#include <liblv/binding.h>

namespace simtix::pipelined {

Lsu::Lsu(const sc_module_name &name)
    : sc_module(name), dmem_port_("dmem_port") {}

void Lsu::SetupAtomicExtensions(
    const sc_bv_base &tmask,
    std::vector<std::unique_ptr<AtomicExtension>> &exts,
    std::vector<tlm::tlm_generic_payload> &trans, AtomicExtension::Op op) {
  assert(exts.size() == trans.size());
  for (uint32_t i = 0; i < trans.size(); ++i) {
    if (tmask[i] == 1) {
      exts[i]->op = op;
      trans[i].set_extension(exts[i].get());
    }
  }
}

void Lsu::ClearAtomicExtensions(std::vector<tlm::tlm_generic_payload> &trans) {
  for (auto &payload : trans) {
    payload.clear_extension<AtomicExtension>();
  }
}

LV_BINDING(simtix, Lsu);

}  // namespace simtix::pipelined
