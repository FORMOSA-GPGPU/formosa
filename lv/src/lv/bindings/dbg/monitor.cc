// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "monitor.h"

#include <liblv/binding.h>
#include <liblv/log.h>

#include <memory>
#include <string_view>

namespace dbg {

Monitor::Monitor(const sc_module_name &name)
    : sc_module(name),
      link_("link",
            [this](tlm::tlm_generic_payload &trans, const tlm::tlm_phase &phase,
                   std::optional<tlm::tlm_sync_enum> status, const sc_time &) {
              PrintPayload(trans, phase, status);
            }) {}

void Monitor::PrintPayload(const tlm::tlm_generic_payload &trans,
                           const tlm::tlm_phase &phase,
                           std::optional<tlm::tlm_sync_enum> status) {
  constexpr std::string_view kForward = "(from -> to)";
  constexpr std::string_view kBackward = "(to -> from)";
  std::string_view phase_name;
  std::string_view direction;
  switch (phase) {
    case tlm::BEGIN_REQ:
      phase_name = "BEGIN_REQ";
      direction = kForward;
      break;
    case tlm::END_REQ:
      phase_name = "END_REQ";
      direction = kBackward;
      break;
    case tlm::BEGIN_RESP:
      phase_name = "BEGIN_RESP";
      direction = kBackward;
      break;
    case tlm::END_RESP:
      phase_name = "END_RESP";
      direction = kForward;
      break;
    default:
      phase_name = "Unknown phase";
      direction = "(unknown)";
      break;
  }
  if (status == tlm::TLM_COMPLETED) {
    // TLM_COMPLETED carries no phase. It returns the call that carried
    // `phase`, so it travels the other way.
    phase_name = "COMPLETED";
    if (direction == kForward) {
      direction = kBackward;
    } else if (direction == kBackward) {
      direction = kForward;
    }
  }
  sc_dt::uint64 addr = trans.get_address();
  tlm::tlm_command cmd = trans.get_command();
  unsigned int len = trans.get_data_length();
  LV_INFO("[{}] {:<10} {:<12} {:<5} {:#x}+{}", name(), phase_name, direction,
          cmd ? "Write" : "Read", addr, len);
}

LV_BINDING(dbg, Monitor)
    .constructor(
        [](const char *name) {
          return std::make_shared<Monitor>(name);
        },
        lv::params("name"), lv::doc("Create a TLM transaction monitor"))
    .property("from", &Monitor::from, lv::doc("Incoming TLM target socket"))
    .property("to", &Monitor::to, &Monitor::set_to,
              lv::doc("Outgoing TLM target"));

}  // namespace dbg
