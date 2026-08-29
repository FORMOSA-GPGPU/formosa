/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <systemc.h>
#include <tlm.h>
#include <tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h>
#include <tlm_utils/peq_with_get.h>
#include <tlm_utils/simple_target_socket.h>

#include <unordered_set>

namespace lv {

class TlmSink : public sc_module {
 public:
  using DbgTransportFunc =
      std::function<unsigned int(tlm::tlm_generic_payload &)>;  // NOLINT

  tlm_utils::simple_target_socket<TlmSink> port;

  sc_export<sc_fifo_in_if<tlm::tlm_generic_payload *>> req_port;
  sc_export<sc_fifo_out_if<tlm::tlm_generic_payload *>> resp_port;

  explicit TlmSink(
      const sc_module_name &name,
      DbgTransportFunc cb =
          [](tlm::tlm_generic_payload &) {
            return 0;
          },
      uint32_t fifo_size = 16);
  ~TlmSink() override = default;

 private:
  uint32_t fifo_size_;
  sc_fifo<tlm::tlm_generic_payload *> req_fifo_;
  sc_fifo<tlm::tlm_generic_payload *> resp_fifo_;

  // Payload event queue for the tlm forward transport
  tlm_utils::peq_with_get<tlm::tlm_generic_payload> peq_;

  // Store the transactions that are waiting for END_REQ phase
  std::unordered_set<tlm::tlm_generic_payload *> pending_end_responses_;

  // Store the transaction that is waiting for request retry
  tlm::tlm_generic_payload *req_retry_trans_ = nullptr;

  DbgTransportFunc transport_dbg_;

  virtual tlm::tlm_sync_enum nb_transport_fw(tlm::tlm_generic_payload &trans,
                                             tlm::tlm_phase &phase,
                                             sc_time &delay);

  virtual unsigned int transport_dbg(tlm::tlm_generic_payload &trans);

  void RequestMethod();
  void ResponseMethod();

  // helpers
  bool TryProcessRequest(tlm::tlm_generic_payload *trans);
};

}  // namespace lv
