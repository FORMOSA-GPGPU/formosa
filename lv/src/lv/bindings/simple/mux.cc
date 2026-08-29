// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "mux.h"

#include <liblv/binding.h>

namespace simple {

Mux::Mux(const sc_core::sc_module_name &name, const Param &param)
    : sc_module(name),
      from("from"),
      to("to"),
      peq_fw_("peq_fw"),
      peq_bw_("peq_bw"),
      req_q_(param.fifo_size),
      resp_q_(param.fifo_size) {
  /* Register non-blocking transport callbacks */
  from.register_nb_transport_fw(this, &Mux::nb_transport_fw);
  from.register_transport_dbg(this, &Mux::transport_dbg);
  to.register_nb_transport_bw(this, &Mux::nb_transport_bw);

  /* Define the main cycle-based process */
  SC_THREAD(ForwardRequest);
  SC_THREAD(ForwardResponse);
  SC_THREAD(HandleRequest);
  SC_THREAD(HandleResponse);
}

tlm::tlm_sync_enum Mux::nb_transport_fw(int id, tlm::tlm_generic_payload &trans,
                                        tlm::tlm_phase &phase,
                                        sc_core::sc_time &delay) {
  switch (phase) {
    case tlm::BEGIN_REQ:
      trans_to_id_[&trans] = id;
      peq_fw_.notify(trans);
      return tlm::TLM_ACCEPTED;
    case tlm::END_RESP:
      resp_end_event_.notify();
      return tlm::TLM_COMPLETED;
    default:
      SC_REPORT_FATAL("Mux", "Illegal transaction phase received");
      return tlm::TLM_COMPLETED;
  }
}

void Mux::HandleRequest() {
  for (;;) {
    wait(peq_fw_.get_event());

    tlm::tlm_generic_payload *trans = nullptr;
    while ((trans = peq_fw_.get_next_transaction()) != nullptr) {
      trans->acquire();
      req_q_.put(trans);

      // Reply the request when the transaction is really put to the req_q_
      auto it = trans_to_id_.find(trans);
      if (it == trans_to_id_.end()) {
        SC_REPORT_FATAL("Mux", "Cannot find original id for the transaction");
      }

      int id = it->second;
      tlm::tlm_phase phase = tlm::END_REQ;
      sc_time delay = SC_ZERO_TIME;
      from[id]->nb_transport_bw(*trans, phase, delay);
    }
  }
}

void Mux::ForwardRequest() {
  for (;;) {
    tlm::tlm_generic_payload *trans = req_q_.get();

    tlm::tlm_phase phase = tlm::BEGIN_REQ;
    sc_time delay = SC_ZERO_TIME;
    to->nb_transport_fw(*trans, phase, delay);
    wait(req_end_event_);
    trans->release();
  }
}

tlm::tlm_sync_enum Mux::nb_transport_bw(tlm::tlm_generic_payload &trans,
                                        tlm::tlm_phase &phase,
                                        sc_core::sc_time &delay) {
  switch (phase) {
    case tlm::END_REQ:
      req_end_event_.notify();
      return tlm::TLM_UPDATED;
    case tlm::BEGIN_RESP:
      peq_bw_.notify(trans);
      return tlm::TLM_ACCEPTED;
    default:
      SC_REPORT_FATAL("Mux", "Illegal transaction phase received");
      return tlm::TLM_COMPLETED;
  }
}

void Mux::HandleResponse() {
  for (;;) {
    wait(peq_bw_.get_event());

    tlm::tlm_generic_payload *trans = nullptr;
    while ((trans = peq_bw_.get_next_transaction()) != nullptr) {
      trans->acquire();
      resp_q_.put(trans);

      // Reply the response when the transaction is really put to resp_q_
      tlm::tlm_phase phase = tlm::END_RESP;
      sc_time delay = SC_ZERO_TIME;
      to->nb_transport_fw(*trans, phase, delay);
    }
  }
}

void Mux::ForwardResponse() {
  for (;;) {
    tlm::tlm_generic_payload *trans = resp_q_.get();

    auto it = trans_to_id_.find(trans);
    if (it == trans_to_id_.end()) {
      SC_REPORT_FATAL("Mux", "Cannot find original id for the transaction");
    }

    int id = it->second;
    tlm::tlm_phase phase = tlm::BEGIN_RESP;
    sc_time delay = SC_ZERO_TIME;
    from[id]->nb_transport_bw(*trans, phase, delay);
    wait(resp_end_event_);
    trans_to_id_.erase(it);
    trans->release();
  }
}

unsigned int Mux::transport_dbg(int id, tlm::tlm_generic_payload &trans) {
  return to->transport_dbg(trans);
}

LV_BINDING(simple, Mux)
    .constructor(
        [](const char *name, const Mux::Param &param) {
          return std::make_shared<Mux>(name, param);
        },
        lv::params("name", "param"), lv::doc("Create a TLM mux"))
    .property("from", &Mux::from, lv::doc("Multi-target input socket"))
    .property("to", &Mux::set_to, lv::doc("Output memory target"));

} /* namespace simple */
