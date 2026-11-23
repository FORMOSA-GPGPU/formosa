/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

// TLM endpoints for tests that drive and observe a component's transport
// interface. Each follows the TLM-2.0 base protocol, and its options pick
// which legal path it takes, so a test reaches paths that the simulator's own
// initiators and targets never take. dbg.PassThroughTester is built on them,
// and C++ tests can include this header directly.
//
// Lua passes TLM sockets around as Initiator::TargetSocket, the common base of
// the target sockets, so the endpoints also connect to components that a test
// builds through their Lua bindings.
//
// The endpoints never throw from a transport call: they record what breaks
// the base protocol in violations(), which a test checks. That includes a call
// or return that moves a transaction's effective time, sc_time_stamp() plus
// its annotated delay, back, which TLM-2.0 forbids on a socket.

#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/peq_with_cb_and_phase.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include <array>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace dbg::tlm_test {

using Payload = tlm::tlm_generic_payload;

/** @brief Throw a std::runtime_error with @p message unless @p condition. */
inline void Check(bool condition, const std::string &message) {
  if (!condition) throw std::runtime_error(message);
}

/**
 * @brief The effective times of the transactions in flight on one socket.
 *
 * A BEGIN_REQ starts a transaction and sets its time; every later call or
 * return on the socket must not move it back.
 */
class Timeline {
 public:
  void Start(const Payload &trans, const sc_time &delay) {
    times_[&trans] = sc_time_stamp() + delay;
  }

  /** @brief Record a call or return, @p what, and its annotated delay. */
  void Advance(const Payload &trans, const sc_time &delay, const char *what,
               std::vector<std::string> &violations) {
    const sc_time time = sc_time_stamp() + delay;
    auto it = times_.find(&trans);
    if (it == times_.end()) {
      times_.emplace(&trans, time);
      return;
    }
    if (time < it->second) {
      violations.push_back(std::string(what) +
                           " moved the transaction's time back");
    } else {
      it->second = time;
    }
  }

  void End(const Payload &trans) { times_.erase(&trans); }

 private:
  std::unordered_map<const Payload *, sc_time> times_;
};

/**
 * @brief A payload that owns its data and byte-enable buffers.
 *
 * The payload frees the extensions set on it when the transaction is
 * destroyed, so a test can hand it ones allocated with new.
 */
class Transaction {
 public:
  Transaction(tlm::tlm_command command, uint64_t address,
              std::vector<unsigned char> data)
      : data_(std::move(data)) {
    payload_.set_command(command);
    payload_.set_address(address);
    payload_.set_data_ptr(data_.empty() ? nullptr : data_.data());
    payload_.set_data_length(data_.size());
    payload_.set_streaming_width(data_.size());
    payload_.set_byte_enable_ptr(nullptr);
    payload_.set_byte_enable_length(0);
    payload_.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
  }
  Transaction(const Transaction &) = delete;
  Transaction &operator=(const Transaction &) = delete;

  /** @brief Set the byte enables, which repeat over the data. */
  Transaction &ByteEnables(std::vector<unsigned char> enables) {
    enables_ = std::move(enables);
    payload_.set_byte_enable_ptr(enables_.data());
    payload_.set_byte_enable_length(enables_.size());
    return *this;
  }

  Transaction &StreamingWidth(unsigned int width) {
    payload_.set_streaming_width(width);
    return *this;
  }

  /** @brief Set an extension, which the payload frees. */
  template <typename T>
  Transaction &Extension(T *extension) {
    payload_.set_extension(extension);
    return *this;
  }

  Payload &payload() { return payload_; }

 private:
  std::vector<unsigned char> data_;
  std::vector<unsigned char> enables_;
  Payload payload_;
};

/**
 * @brief Issues transactions from the caller's context, such as sc_main or
 * Lua between sc_start calls.
 *
 * A response that arrives as a BEGIN_RESP call is completed by returning
 * TLM_COMPLETED. A response returned as TLM_UPDATED with BEGIN_RESP is
 * completed at once with END_RESP, at the response's effective time.
 */
class Initiator : public sc_module {
 public:
  using Socket = tlm_utils::simple_initiator_socket<Initiator>;
  /** @brief The target socket type that Lua passes around as sc.Socket. */
  using TargetSocket = Socket::base_target_socket_type;

  Socket socket{"socket"};
  /** @brief Observe target-initiated DMI invalidation, if needed by a test. */
  std::function<void(sc_dt::uint64, sc_dt::uint64)> on_invalidate;

  explicit Initiator(const sc_module_name &name) : sc_module(name) {
    socket.register_nb_transport_bw(this, &Initiator::Backward);
    socket.register_invalidate_direct_mem_ptr(this, &Initiator::Invalidate);
  }

  /** @brief Send BEGIN_REQ; @p delay holds the returned delay afterwards. */
  tlm::tlm_sync_enum Send(Payload &trans, sc_time &delay) {
    tlm::tlm_phase phase = tlm::BEGIN_REQ;
    timeline_.Start(trans, delay);
    const tlm::tlm_sync_enum status =
        socket->nb_transport_fw(trans, phase, delay);
    timeline_.Advance(trans, delay, "the BEGIN_REQ return", violations_);
    returned_phase_ = phase;
    if (status == tlm::TLM_UPDATED && phase == tlm::BEGIN_RESP) {
      tlm::tlm_phase end = tlm::END_RESP;
      sc_time end_delay = delay;
      if (socket->nb_transport_fw(trans, end, end_delay) !=
          tlm::TLM_COMPLETED) {
        violations_.push_back("the target did not complete END_RESP");
      }
      timeline_.Advance(trans, end_delay, "the END_RESP return", violations_);
    }
    if (status != tlm::TLM_ACCEPTED) timeline_.End(trans);
    return status;
  }

  tlm::tlm_sync_enum Send(Payload &trans) {
    sc_time delay = SC_ZERO_TIME;
    return Send(trans, delay);
  }

  void SendBlocking(Payload &trans, sc_time &delay) {
    socket->b_transport(trans, delay);
  }

  bool RequestDmi(Payload &trans) {
    tlm::tlm_dmi dmi;
    return RequestDmi(trans, dmi);
  }

  /** @brief Request DMI; @p dmi holds the target's descriptor afterwards. */
  bool RequestDmi(Payload &trans, tlm::tlm_dmi &dmi) {
    return socket->get_direct_mem_ptr(trans, dmi);
  }

  unsigned int Debug(Payload &trans) { return socket->transport_dbg(trans); }

  /** @brief Phase that the latest Send got back with its status. */
  tlm::tlm_phase returned_phase() const { return returned_phase_; }
  /** @brief Number of BEGIN_RESP calls received. */
  int responses() const { return responses_; }
  /** @brief Delay the latest BEGIN_RESP call arrived with. */
  sc_time response_delay() const { return response_delay_; }
  const std::vector<std::string> &violations() const { return violations_; }

 private:
  void Invalidate(sc_dt::uint64 start, sc_dt::uint64 end) {
    if (on_invalidate) on_invalidate(start, end);
  }

  tlm::tlm_sync_enum Backward(Payload &trans, tlm::tlm_phase &phase,
                              sc_time &delay) {
    if (phase != tlm::BEGIN_RESP) {
      violations_.push_back(std::string("the initiator received ") +
                            phase.get_name());
      return tlm::TLM_ACCEPTED;
    }
    timeline_.Advance(trans, delay, "BEGIN_RESP", violations_);
    timeline_.End(trans);
    ++responses_;
    response_delay_ = delay;
    return tlm::TLM_COMPLETED;
  }

  Timeline timeline_;
  tlm::tlm_phase returned_phase_ = tlm::UNINITIALIZED_PHASE;
  int responses_ = 0;
  sc_time response_delay_;
  std::vector<std::string> violations_;
};

/**
 * @brief Answers every request on one legal base-protocol path.
 *
 * - kComplete returns TLM_COMPLETED for BEGIN_REQ, adding added_delay to the
 *   annotated delay.
 * - kUpdate returns TLM_UPDATED with BEGIN_RESP, adding added_delay, and
 *   completes the END_RESP that follows.
 * - kRespondLater returns TLM_ACCEPTED and sends BEGIN_RESP, with no
 *   annotated delay, response_latency after the request's effective time.
 *
 * b_transport answers after adding added_delay, debug transport answers with
 * the data length, and a DMI request is granted over a 256-byte buffer. The
 * response status comes from response_status.
 */
class Target : public sc_module {
 public:
  enum class Mode { kComplete, kUpdate, kRespondLater };

  struct Options {
    Mode mode = Mode::kComplete;
    sc_time response_latency = SC_ZERO_TIME;
    sc_time added_delay = SC_ZERO_TIME;
    std::function<tlm::tlm_response_status(const Payload &)> response_status =
        [](const Payload &) {
          return tlm::TLM_OK_RESPONSE;
        };
  };

  tlm_utils::simple_target_socket<Target> socket{"socket"};
  /** @brief Changes apply to the next request. */
  Options options;

  explicit Target(const sc_module_name &name) : Target(name, Options()) {}

  Target(const sc_module_name &name, Options options)
      : sc_module(name),
        options(std::move(options)),
        events_(this, &Target::Respond) {
    socket.register_nb_transport_fw(this, &Target::Forward);
    socket.register_b_transport(this, &Target::Blocking);
    socket.register_get_direct_mem_ptr(this, &Target::Dmi);
    socket.register_transport_dbg(this, &Target::Debug);
  }

  /** @brief Number of BEGIN_REQ calls received. */
  int requests() const { return requests_; }
  /** @brief Number of END_RESP calls received. */
  int end_responses() const { return end_responses_; }
  /** @brief Number of BEGIN_RESP calls sent whose call has returned. */
  int responses_sent() const { return responses_sent_; }
  /** @brief Whether a BEGIN_RESP call is still in progress. */
  bool responding() const { return responding_; }
  /** @brief Delay the latest BEGIN_RESP call was sent with. */
  sc_time response_delay() const { return response_delay_; }
  int blocking_calls() const { return blocking_calls_; }
  int dmi_requests() const { return dmi_requests_; }
  int debug_calls() const { return debug_calls_; }
  /** @brief Payload of the latest call. */
  const Payload *last_payload() const { return last_payload_; }
  /** @brief Delay the latest BEGIN_REQ or b_transport call arrived with. */
  sc_time received_delay() const { return received_delay_; }
  /** @brief Delay the latest BEGIN_REQ or b_transport call returned. */
  sc_time returned_delay() const { return returned_delay_; }
  /** @brief Status returned for the latest BEGIN_REQ. */
  tlm::tlm_sync_enum returned_status() const { return returned_status_; }
  /** @brief Status the initiator returned for the latest BEGIN_RESP. */
  tlm::tlm_sync_enum response_status() const { return response_status_; }
  const std::vector<std::string> &violations() const { return violations_; }

 private:
  tlm::tlm_sync_enum Forward(Payload &trans, tlm::tlm_phase &phase,
                             sc_time &delay) {
    last_payload_ = &trans;
    if (phase == tlm::END_RESP) {
      timeline_.Advance(trans, delay, "END_RESP", violations_);
      timeline_.End(trans);
      ++end_responses_;
      return tlm::TLM_COMPLETED;
    }
    if (phase != tlm::BEGIN_REQ) {
      violations_.push_back(std::string("the target received ") +
                            phase.get_name());
      return tlm::TLM_ACCEPTED;
    }
    ++requests_;
    received_delay_ = delay;
    timeline_.Start(trans, delay);
    switch (options.mode) {
      case Mode::kComplete:
        delay += options.added_delay;
        trans.set_response_status(options.response_status(trans));
        returned_status_ = tlm::TLM_COMPLETED;
        break;
      case Mode::kUpdate:
        delay += options.added_delay;
        trans.set_response_status(options.response_status(trans));
        phase = tlm::BEGIN_RESP;
        returned_status_ = tlm::TLM_UPDATED;
        break;
      case Mode::kRespondLater:
        events_.notify(trans, tlm::BEGIN_RESP,
                       delay + options.response_latency);
        returned_status_ = tlm::TLM_ACCEPTED;
        break;
    }
    returned_delay_ = delay;
    if (returned_status_ == tlm::TLM_COMPLETED) timeline_.End(trans);
    return returned_status_;
  }

  void Respond(Payload &trans, const tlm::tlm_phase &) {
    trans.set_response_status(options.response_status(trans));
    tlm::tlm_phase phase = tlm::BEGIN_RESP;
    sc_time delay = SC_ZERO_TIME;
    timeline_.Advance(trans, delay, "BEGIN_RESP", violations_);
    response_delay_ = delay;
    // The response counts as sent once the call returns: until then, the
    // payload is still in use on the way to the initiator.
    responding_ = true;
    response_status_ = socket->nb_transport_bw(trans, phase, delay);
    responding_ = false;
    ++responses_sent_;
    if (response_status_ == tlm::TLM_COMPLETED) timeline_.End(trans);
  }

  void Blocking(Payload &trans, sc_time &delay) {
    last_payload_ = &trans;
    ++blocking_calls_;
    received_delay_ = delay;
    delay += options.added_delay;
    returned_delay_ = delay;
    trans.set_response_status(options.response_status(trans));
  }

  bool Dmi(Payload &trans, tlm::tlm_dmi &dmi) {
    ++dmi_requests_;
    dmi.set_dmi_ptr(memory_.data());
    dmi.set_start_address(0);
    dmi.set_end_address(memory_.size() - 1);
    dmi.allow_read_write();
    trans.set_dmi_allowed(true);
    return true;
  }

  unsigned int Debug(Payload &trans) {
    last_payload_ = &trans;
    ++debug_calls_;
    return trans.get_data_length();
  }

  tlm_utils::peq_with_cb_and_phase<Target> events_;
  Timeline timeline_;
  std::array<unsigned char, 0x100> memory_{};
  int requests_ = 0;
  int end_responses_ = 0;
  int responses_sent_ = 0;
  bool responding_ = false;
  sc_time response_delay_;
  int blocking_calls_ = 0;
  int dmi_requests_ = 0;
  int debug_calls_ = 0;
  const Payload *last_payload_ = nullptr;
  sc_time received_delay_;
  sc_time returned_delay_;
  tlm::tlm_sync_enum returned_status_ = tlm::TLM_ACCEPTED;
  tlm::tlm_sync_enum response_status_ = tlm::TLM_ACCEPTED;
  std::vector<std::string> violations_;
};

}  // namespace dbg::tlm_test
