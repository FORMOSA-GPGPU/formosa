// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "monitor.h"

#include <liblv/binding.h>
#include <liblv/log.h>

#include <memory>

namespace dbg {

Monitor::Monitor(const sc_module_name &name)
    : sc_module(name), from_("from"), to_("to") {
  from_.register_nb_transport_fw(this, &Monitor::nb_transport_fw);
  to_.register_nb_transport_bw(this, &Monitor::nb_transport_bw);

  from_.register_transport_dbg(this, &Monitor::transport_dbg);
}

tlm::tlm_sync_enum Monitor::nb_transport_fw(tlm::tlm_generic_payload &trans,
                                            tlm::tlm_phase &phase,
                                            sc_time &delay) {
  to_->nb_transport_fw(trans, phase, delay);
  PrintPayload(trans, phase, delay);
  return tlm::TLM_ACCEPTED;
}

tlm::tlm_sync_enum Monitor::nb_transport_bw(tlm::tlm_generic_payload &trans,
                                            tlm::tlm_phase &phase,
                                            sc_time &delay) {
  from_->nb_transport_bw(trans, phase, delay);
  PrintPayload(trans, phase, delay);
  return tlm::TLM_ACCEPTED;
}

unsigned int Monitor::transport_dbg(tlm::tlm_generic_payload &trans) {
  return to_->transport_dbg(trans);
}

void Monitor::PrintPayload(tlm::tlm_generic_payload &trans,
                           tlm::tlm_phase &phase, sc_time &delay) {
  std::string_view phase_name;
  std::string_view direction;
  switch (phase) {
    case tlm::BEGIN_REQ:
      phase_name = "BEGIN_REQ";
      direction = "(from -> to)";
      break;
    case tlm::END_REQ:
      phase_name = "END_REQ";
      direction = "(to -> from)";
      break;
    case tlm::BEGIN_RESP:
      phase_name = "BEGIN_RESP";
      direction = "(to -> from)";
      break;
    case tlm::END_RESP:
      phase_name = "END_RESP";
      direction = "(from -> to)";
      break;
    default:
      phase_name = "Unknown phase";
      direction = "(unknown)";
      break;
  }
  sc_dt::uint64 addr = trans.get_address();
  tlm::tlm_command cmd = trans.get_command();
  unsigned int len = trans.get_data_length();
  LV_INFO("[{}] {:<10} {:<12} {:<5} {:#x}+{}", name(), phase_name, direction,
          cmd ? "Write" : "Read", addr, len);
}

void Monitor::set_to(Target *target) {
  target_ = target;
  to_.bind(*target_);
}

Monitor::Target *Monitor::to() const { return target_; }

LV_BINDING(dbg, Monitor)
    .constructor(
        [](const char *name) {
          return std::make_shared<Monitor>(name);
        },
        lv::params("name"), lv::doc("Create a TLM transaction monitor"))
    .property("from", &Monitor::from_, lv::doc("Incoming TLM target socket"))
    .property("to", &Monitor::to, &Monitor::set_to,
              lv::doc("Outgoing TLM target"));

}  // namespace dbg
