// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>
#include <liblv/common/tlm_sink.h>
#include <liblv/output.h>
#include <tlm_core/tlm_2/tlm_2_interfaces/tlm_fw_bw_ifs.h>
#include <tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h>

namespace lv {

TlmSink::TlmSink(const sc_module_name &name, DbgTransportFunc cb,
                 uint32_t fifo_size)
    : sc_module(name),
      port("port"),
      fifo_size_(fifo_size),
      req_fifo_(fifo_size),
      resp_fifo_(fifo_size),
      peq_("peq"),
      transport_dbg_(cb) {
  req_port.bind(req_fifo_);
  resp_port.bind(resp_fifo_);

  port.register_nb_transport_fw(this, &TlmSink::nb_transport_fw);
  port.register_transport_dbg(this, &TlmSink::transport_dbg);

  SC_METHOD(RequestMethod);
  sensitive << peq_.get_event();
  dont_initialize();

  SC_METHOD(ResponseMethod);
  sensitive << resp_fifo_.data_written_event();
  dont_initialize();
}

tlm::tlm_sync_enum TlmSink::nb_transport_fw(tlm::tlm_generic_payload &trans,
                                            tlm::tlm_phase &phase,
                                            sc_time &delay) {
  switch (phase) {
    case tlm::BEGIN_REQ: {
      peq_.notify(trans, delay);
      return tlm::TLM_ACCEPTED;
    } break;
    case tlm::END_RESP:
      // Remove from pending responses
      if (pending_end_responses_.find(&trans) == pending_end_responses_.end()) {
        SC_REPORT_ERROR("TLM_Sink",
                        "Received END_RESP for unknown transaction");
        // Return completed status for unknown transaction
        return tlm::TLM_COMPLETED;
      }
      pending_end_responses_.erase(&trans);
      return tlm::TLM_ACCEPTED;
      break;
    default:
      SC_REPORT_FATAL("TLM_Sink", "Illegal transaction phase received");
      // Return completed status for illegal phase
      return tlm::TLM_COMPLETED;
      break;
  }
  // Notify the payload event queue with no delay
  return tlm::TLM_COMPLETED;  // Return accepted status
}

unsigned int TlmSink::transport_dbg(tlm::tlm_generic_payload &trans) {
  return transport_dbg_(trans);
}

/**
 * @brief Method to handle peq_delayed BEGIN_REQ transactions.
 *
 * This method serves the request path between the PEQ and the request FIFO.
 *
 * 1. **Retry Handling**: If a transaction was previously deferred due to a full
 *    FIFO (stored in `req_retry_trans_`), it attempts to process it first.
 * 2. **PEQ Processing**: It drains all currently available transactions from
 *    the PEQ and attempts to move them into the request FIFO.
 * 3. **Back-pressure Management**: If the request FIFO is full, the method
 *    stores the current transaction and uses `next_trigger` to suspend
 *    execution until the FIFO has space (signaled by `data_read_event()`).
 * 4. **Phase Acknowledgement**: Once a transaction is successfully enqueued, it
 *    triggers the `END_REQ` phase via `nb_transport_bw` to acknowledge the
 *    request to the initiator.
 */
void TlmSink::RequestMethod() {
  tlm::tlm_generic_payload *trans = nullptr;

  if (req_retry_trans_) {
    if (!TryProcessRequest(req_retry_trans_)) {
      // Still cannot process the transaction, wait for the next event
      next_trigger(req_fifo_.data_read_event());
      return;
    }
    // Successfully processed the transaction, clear the retry transaction
    req_retry_trans_ = nullptr;
  }

  while ((trans = peq_.get_next_transaction()) != nullptr) {
    if (!TryProcessRequest(trans)) {
      // Cannot process the transaction, store it for retry and wait for the
      // next event
      req_retry_trans_ = trans;
      next_trigger(req_fifo_.data_read_event());
      return;
    }
  }
}

/**
 * @brief Helper to enqueue a request and acknowledge the phase transition.
 *
 * This utility function encapsulates the logic for moving a transaction
 * into the request FIFO and notifying the initiator that the request
 * has been accepted.
 *
 * @param trans Pointer to the TLM generic payload to be processed.
 * @return true If the transaction was successfully written to the FIFO
 * and the `END_REQ` phase was sent.
 * @return false If the FIFO is full (back-pressure), indicating the
 * caller should retry later.
 */
bool TlmSink::TryProcessRequest(tlm::tlm_generic_payload *trans) {
  if (req_fifo_.nb_write(trans)) {
    sc_time delay = SC_ZERO_TIME;
    tlm::tlm_phase phase = tlm::END_REQ;
    port->nb_transport_bw(*trans, phase, delay);
    return true;
  }
  return false;
}

/**
 * @brief Processes response transactions from the internal response FIFO.
 *
 * This method acts as the target's response dispatcher. It continuously
 * reads transactions from the `resp_fifo_` and initiates the response
 * phase of the TLM-2.0 handshake.
 *
 * 1. **FIFO Consumption**: Drains transactions from the response FIFO as
 * long as they are available.
 * 2. **Phase Initiation**: For each transaction, it triggers `nb_transport_bw`
 * with the `BEGIN_RESP` phase.
 * 3. **Tracking**: Adds the transaction to `pending_end_responses_` to
 * await the corresponding `END_RESP` from the initiator.
 * * @note This method is sensitive to `resp_fifo_.data_written_event()`.
 */
void TlmSink::ResponseMethod() {
  tlm::tlm_generic_payload *trans = nullptr;
  while (resp_fifo_.nb_read(trans)) {
    sc_time delay = SC_ZERO_TIME;
    tlm::tlm_phase phase = tlm::BEGIN_RESP;
    port->nb_transport_bw(*trans, phase, delay);

    pending_end_responses_.insert(trans);
  }
}

LV_BINDING(simple, TlmSink)
    .property("port", &TlmSink::port, lv::doc("Incoming TLM target socket"));

}  // namespace lv
