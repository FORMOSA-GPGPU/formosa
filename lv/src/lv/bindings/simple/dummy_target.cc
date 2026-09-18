// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>
#include <liblv/log.h>
#include <liblv/schema.h>
#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/peq_with_get.h>
#include <tlm_utils/simple_target_socket.h>

#include <memory>

namespace simple {

class DummyTarget : public sc_module {
 public:
  struct Param {
    bool verbose = false;
    LV_SCHEMA(DummyTarget, Param,
              LV_FIELD(verbose, "Log discarded transactions"))
  };

  DummyTarget(const sc_module_name &name, const Param &param)
      : sc_module(name), port_("port"), peq_("peq"), verbose_(param.verbose) {
    port_.register_b_transport(this, &DummyTarget::b_transport);
    port_.register_nb_transport_fw(this, &DummyTarget::nb_transport_fw);
    port_.register_transport_dbg(this, &DummyTarget::transport_dbg);

    SC_METHOD(ProcessRequests);
    sensitive << peq_.get_event();
    dont_initialize();
  }

  auto port() { return &port_; }

 private:
  tlm_utils::simple_target_socket<DummyTarget> port_;
  tlm_utils::peq_with_get<tlm::tlm_generic_payload> peq_;
  bool verbose_;

  void Complete(tlm::tlm_generic_payload &trans) {
    if (verbose_) {
      LV_INFO("[{}] Discarded {} at {:#x}+{}", name(),
              trans.is_read() ? "read" : "write", trans.get_address(),
              trans.get_data_length());
    }
    trans.set_response_status(tlm::TLM_OK_RESPONSE);
  }

  void b_transport(tlm::tlm_generic_payload &trans, sc_time &delay) {
    Complete(trans);
  }

  tlm::tlm_sync_enum nb_transport_fw(tlm::tlm_generic_payload &trans,
                                     tlm::tlm_phase &phase, sc_time &delay) {
    if (phase == tlm::BEGIN_REQ) {
      peq_.notify(trans, delay);
      return tlm::TLM_ACCEPTED;
    }
    return tlm::TLM_COMPLETED;
  }

  unsigned int transport_dbg(tlm::tlm_generic_payload &trans) {
    Complete(trans);
    return trans.get_data_length();
  }

  void ProcessRequests() {
    while (auto *trans = peq_.get_next_transaction()) {
      Complete(*trans);

      sc_time delay = SC_ZERO_TIME;
      tlm::tlm_phase phase = tlm::END_REQ;
      port_->nb_transport_bw(*trans, phase, delay);

      phase = tlm::BEGIN_RESP;
      port_->nb_transport_bw(*trans, phase, delay);
    }
  }
};

LV_BINDING(simple, DummyTarget)
    .constructor(
        [](const char *name, const DummyTarget::Param &param) {
          return std::make_shared<DummyTarget>(name, param);
        },
        lv::params("name", "param"),
        lv::doc("Create a target that discards TLM transactions"))
    .property("port", &DummyTarget::port,
              lv::doc("Incoming TLM target socket"));

}  // namespace simple
