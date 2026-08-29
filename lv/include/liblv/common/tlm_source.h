/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/common/sc_fifo_with_peek.h>
#include <systemc.h>
#include <tlm.h>
#include <tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h>
#include <tlm_utils/peq_with_get.h>
#include <tlm_utils/simple_initiator_socket.h>

#include <unordered_set>

namespace lv {

class TlmSource : public sc_module {
 public:
  explicit TlmSource(const sc_module_name &name, uint32_t fifo_size = 16);
  ~TlmSource() override = default;

  using Target =
      tlm_utils::simple_initiator_socket<TlmSource>::base_target_socket_type;

  tlm_utils::simple_initiator_socket<TlmSource> socket_;

  sc_export<sc_fifo_out_if<tlm::tlm_generic_payload *>> req_port;
  sc_export<sc_fifo_in_if<tlm::tlm_generic_payload *>> resp_port;
  sc_export<tlm::tlm_nonblocking_peek_if<tlm::tlm_generic_payload *>>
      peek_resp_port;

  /**
   * Debug transaction to send a TLM request
   */
  unsigned int PutRequestDbg(tlm::tlm_generic_payload &trans);

  void set_target(Target *t);
  Target *target() const;

 private:
  Target *target_;
  uint32_t fifo_size_;
  sc_fifo<tlm::tlm_generic_payload *> req_fifo_;
  sc_fifo_with_peek<tlm::tlm_generic_payload *> resp_fifo_;

  // Payload event queue for delayed tlm backward transport
  tlm_utils::peq_with_get<tlm::tlm_generic_payload> peq_;

  // Store the transactions that are waiting for END_REQ phase
  std::unordered_set<tlm::tlm_generic_payload *> pending_end_requests_;

  // Store the transaction that is waiting for response retry
  tlm::tlm_generic_payload *resp_retry_trans_ = nullptr;

  tlm::tlm_sync_enum nb_transport_bw(tlm::tlm_generic_payload &trans,
                                     tlm::tlm_phase &phase, sc_time &delay);

  void RequestMethod();
  void ResponseMethod();

  // helpers
  bool TryProcessResponse(tlm::tlm_generic_payload *trans);
};

}  // namespace lv
