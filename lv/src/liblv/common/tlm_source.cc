// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>
#include <liblv/common/tlm_source.h>
#include <tlm_core/tlm_2/tlm_2_interfaces/tlm_fw_bw_ifs.h>
#include <tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h>

namespace lv {

TlmSource::TlmSource(const sc_module_name &name, uint32_t fifo_size)
    : sc_module(name),
      socket_("socket"),
      target_(nullptr),
      fifo_size_(fifo_size),
      req_fifo_("req_fifo", fifo_size),
      resp_fifo_("resp_fifo", fifo_size),
      peq_("peq") {
  req_port.bind(req_fifo_);
  resp_port.bind(resp_fifo_);
  peek_resp_port.bind(resp_fifo_);

  socket_.register_nb_transport_bw(this, &TlmSource::nb_transport_bw);

  SC_METHOD(RequestMethod);
  sensitive << req_fifo_.data_written_event();
  dont_initialize();

  SC_METHOD(ResponseMethod);
  sensitive << peq_.get_event();
  dont_initialize();
}

unsigned int TlmSource::PutRequestDbg(tlm::tlm_generic_payload &trans) {
  return socket_->transport_dbg(trans);
}

void TlmSource::set_target(Target *t) {
  target_ = t;
  if (target_) {
    socket_.bind(*target_);
  } else {
    SC_REPORT_FATAL("TLM_Source", "Target cannot be null");
  }
}

TlmSource::Target *TlmSource::target() const { return target_; }

/**
 * @brief Processes request transactions from the request FIFO.
 *
 * This method acts as the initiator's request dispatcher. It continuously
 * reads transactions from the `req_fifo_` and initiates the TLM-2.0
 * 4-phase handshake by sending a `BEGIN_REQ` phase to the target.
 *
 * 1. **FIFO Consumption**: Drains transactions from the request FIFO as
 *    long as they are available.
 * 2. **Phase Initiation**: For each transaction, it triggers `nb_transport_fw`
 *    with the `BEGIN_REQ` phase.
 * 3. **Tracking**: Adds the transaction to `pending_end_requests_` to
 *    await the corresponding `END_REQ` from the target.
 *
 * @note This method is sensitive to `req_fifo_.data_written_event()`.
 */
void TlmSource::RequestMethod() {
  tlm::tlm_generic_payload *trans = nullptr;
  while ((req_fifo_.nb_read(trans))) {
    sc_time delay = SC_ZERO_TIME;
    tlm::tlm_phase phase = tlm::BEGIN_REQ;
    socket_->nb_transport_fw(*trans, phase, delay);

    pending_end_requests_.insert(trans);
  }
}

tlm::tlm_sync_enum TlmSource::nb_transport_bw(tlm::tlm_generic_payload &trans,
                                              tlm::tlm_phase &phase,
                                              sc_time &delay) {
  switch (phase) {
    case tlm::END_REQ:
      // Remove from pending requests
      if (pending_end_requests_.find(&trans) == pending_end_requests_.end()) {
        SC_REPORT_ERROR("TLM_Source",
                        "Received END_REQ for unknown transaction");
        // Return completed status for unknown transaction
        return tlm::TLM_COMPLETED;
      }
      pending_end_requests_.erase(&trans);
      return tlm::TLM_ACCEPTED;
      break;
    case tlm::BEGIN_RESP:
      peq_.notify(trans, delay);
      return tlm::TLM_ACCEPTED;
      break;
    default:
      // Return completed status for illegal phase
      SC_REPORT_FATAL("TLM_Source", "Illegal transaction phase received");
      return tlm::TLM_COMPLETED;
      break;
  }
}

/**
 * @brief Method to handle peq_delayed BEGIN_RESP transactions.
 *
 * This method serves the response path between the PEQ and the response FIFO.
 *
 * 1. **Retry Handling**: If a transaction was previously deferred due to a full
 *    FIFO (stored in `resp_retry_trans`), it attempts to process it first.
 * 2. **PEQ Processing**: It drains all currently available transactions from
 *    the PEQ and attempts to move them into the response FIFO.
 * 3. **Back-pressure Management**: If the response FIFO is full, the method
 *    stores the current transaction and uses `next_trigger` to suspend
 * execution until the FIFO has space (signaled by `data_read_event()`).
 * 4. **Phase Completion**: Once a transaction is successfully enqueued, it
 *    triggers the `END_RESP` phase via `nb_transport_fw` to complete the
 *    TLM-2.0 4-phase handshake.
 */
void TlmSource::ResponseMethod() {
  tlm::tlm_generic_payload *trans = nullptr;

  if (resp_retry_trans_) {
    if (!TryProcessResponse(resp_retry_trans_)) {
      // Still cannot process the transaction, wait for the next event
      next_trigger(resp_fifo_.data_read_event());
      return;
    }
    // Successfully processed the transaction, clear the retry transaction
    resp_retry_trans_ = nullptr;
  }

  while ((trans = peq_.get_next_transaction()) != nullptr) {
    if (!TryProcessResponse(trans)) {
      // Cannot process the transaction, store it for retry and wait for the
      // next event
      resp_retry_trans_ = trans;
      next_trigger(resp_fifo_.data_read_event());
      return;
    }
  }
}

/**
 * @brief Helper to enqueue a response and complete the phase handshake.
 *
 * This utility function encapsulates the logic for moving a transaction
 * into the response FIFO and notifying the target of phase completion.
 *
 * @param trans Pointer to the TLM generic payload to be processed.
 * @return true If the transaction was successfully written to the FIFO
 * and the `END_RESP` phase was sent.
 * @return false If the FIFO is full (back-pressure), indicating the
 * caller should retry later.
 */
bool TlmSource::TryProcessResponse(tlm::tlm_generic_payload *trans) {
  if (resp_fifo_.nb_write(trans)) {
    sc_time delay = SC_ZERO_TIME;
    tlm::tlm_phase phase = tlm::END_RESP;
    socket_->nb_transport_fw(*trans, phase, delay);
    return true;
  }
  return false;
}

LV_BINDING(simple, TlmSource)
    .property("target", &TlmSource::target, &TlmSource::set_target,
              lv::doc("Outgoing memory target"));

}  // namespace lv
