/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/schema.h>
#include <tlm.h>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <tlm_utils/peq_with_get.h>
#include <tlm_utils/simple_initiator_socket.h>

#include <sol/sol.hpp>
#include <systemc>
#include <unordered_map>

namespace simple {

class Mux : public sc_core::sc_module {
 public:
  struct Param {
    uint32_t fifo_size = 16;
    /* clang-format off */
    LV_SCHEMA(Mux, Param,
              LV_FIELD(fifo_size, "Internal FIFO size"))
    /* clang-format on */
  };

  using TargetSocket = tlm_utils::multi_passthrough_target_socket<Mux>;
  using InitiatorSocket = tlm_utils::simple_initiator_socket<Mux>;

  TargetSocket from;
  InitiatorSocket to;

  Mux(const sc_core::sc_module_name &name, const Param &param);
  ~Mux() override = default;

  void set_to(InitiatorSocket::base_target_socket_type *target) {
    to.bind(*target);
  }

 private:
  /* Internal request buffer */
  tlm_utils::peq_with_get<tlm::tlm_generic_payload> peq_fw_;
  tlm_utils::peq_with_get<tlm::tlm_generic_payload> peq_bw_;
  tlm::tlm_fifo<tlm::tlm_generic_payload *> req_q_;
  tlm::tlm_fifo<tlm::tlm_generic_payload *> resp_q_;

  /* Map to route responses back to the correct master index */
  std::unordered_map<tlm::tlm_generic_payload *, int> trans_to_id_;

  sc_core::sc_event req_end_event_;
  sc_core::sc_event resp_end_event_;

  /* Process that handles transactions on the clock edge */
  void ForwardRequest();
  void ForwardResponse();
  void HandleRequest();
  void HandleResponse();

  /* TLM-2.0 non-blocking transport callbacks */
  tlm::tlm_sync_enum nb_transport_fw(int id, tlm::tlm_generic_payload &trans,
                                     tlm::tlm_phase &phase,
                                     sc_core::sc_time &delay);
  tlm::tlm_sync_enum nb_transport_bw(tlm::tlm_generic_payload &trans,
                                     tlm::tlm_phase &phase,
                                     sc_core::sc_time &delay);

  /* Debug transport callback */
  unsigned int transport_dbg(int id, tlm::tlm_generic_payload &trans);
};

} /* namespace simple */
