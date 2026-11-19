/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/log.h>
#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include <functional>
#include <optional>
#include <utility>

namespace dbg {

/**
 * @brief Transparent TLM link for observers.
 *
 * Every call is forwarded in the caller's context and its status returned
 * unchanged; the module has no process, so inserting it adds no delta cycle
 * and no simulated time.
 *
 * The observer sees what crosses the link: each call with the phase it
 * carries, then a TLM_UPDATED or TLM_COMPLETED return. A phase only implied,
 * by a later phase or by TLM_COMPLETED, is not reported.
 *
 * The non-blocking protocol, b_transport and debug transport pass unchanged;
 * b_transport is reported as a BEGIN_REQ call that TLM_COMPLETED returns. Two
 * cases are not fully supported yet, and the first occurrence of each warns:
 * - A call or return with a nonzero annotated delay, non-blocking or
 *   b_transport: the delay reaches the observer only in `at`, so an observer
 *   that reports the current simulation time reports it early.
 * - A DMI request is refused, so the initiator falls back to transactions:
 *   its data stays correct but its timing differs from the unobserved link.
 *   DMI invalidations are not passed back; no grant crosses this link, so
 *   none of them concerns a grant obtained through it.
 */
class PassThrough : public sc_module {
 public:
  /** @brief Target socket type that the outgoing socket binds to. */
  using Target =
      tlm_utils::simple_initiator_socket<PassThrough>::base_target_socket_type;

  /**
   * @brief Callback invoked for each call and each non-TLM_ACCEPTED return.
   *
   * It receives the payload, a phase, a status, and `at`, which is
   * sc_time_stamp() plus the annotated delay:
   * - for a call, before it is passed on: the phase it carries and no status;
   * - for a TLM_UPDATED return: the phase the return carries and TLM_UPDATED;
   * - for a TLM_COMPLETED return: the phase of the call it returns from and
   *   TLM_COMPLETED.
   *
   * A TLM_ACCEPTED return carries nothing and is not reported.
   */
  using Observer =
      std::function<void(tlm::tlm_generic_payload &, const tlm::tlm_phase &,
                         std::optional<tlm::tlm_sync_enum>, const sc_time &at)>;

  /**
   * @brief Create a link that reports to an observer.
   *
   * @param name Module name.
   * @param observer Callback invoked for each call and each non-TLM_ACCEPTED
   *     return that crosses the link.
   */
  PassThrough(const sc_module_name &name, Observer observer)
      : sc_module(name),
        from("from"),
        to("to"),
        observer_(std::move(observer)) {
    from.register_nb_transport_fw(this, &PassThrough::nb_transport_fw);
    from.register_b_transport(this, &PassThrough::b_transport);
    from.register_get_direct_mem_ptr(this, &PassThrough::get_direct_mem_ptr);
    from.register_transport_dbg(this, &PassThrough::transport_dbg);
    to.register_nb_transport_bw(this, &PassThrough::nb_transport_bw);
  }

  /**
   * @brief Bind the outgoing socket to a target.
   *
   * @param target Target socket that receives the passed-on calls.
   */
  void bind(Target *target) {
    target_ = target;
    to.bind(*target_);
  }

  /**
   * @brief Return the target given to bind().
   *
   * @return The bound target, or nullptr before bind().
   */
  Target *target() const { return target_; }

  /** @brief Incoming socket that initiators bind to. */
  tlm_utils::simple_target_socket<PassThrough> from;
  /** @brief Outgoing socket, bound to the target by bind(). */
  tlm_utils::simple_initiator_socket<PassThrough> to;

 private:
  tlm::tlm_sync_enum nb_transport_fw(tlm::tlm_generic_payload &trans,
                                     tlm::tlm_phase &phase, sc_time &delay) {
    return Pass(trans, phase, delay, [&] {
      return to->nb_transport_fw(trans, phase, delay);
    });
  }

  tlm::tlm_sync_enum nb_transport_bw(tlm::tlm_generic_payload &trans,
                                     tlm::tlm_phase &phase, sc_time &delay) {
    return Pass(trans, phase, delay, [&] {
      return from->nb_transport_bw(trans, phase, delay);
    });
  }

  void b_transport(tlm::tlm_generic_payload &trans, sc_time &delay) {
    Report(trans, tlm::BEGIN_REQ, std::nullopt, delay);
    const bool managed = trans.has_mm();
    if (managed) trans.acquire();
    to->b_transport(trans, delay);
    Report(trans, tlm::BEGIN_REQ, tlm::TLM_COMPLETED, delay);
    if (managed) trans.release();
  }

  bool get_direct_mem_ptr(tlm::tlm_generic_payload &trans, tlm::tlm_dmi &) {
    if (!warned_dmi_) {
      warned_dmi_ = true;
      LV_WARNING(
          "{}: refused a DMI request; the initiator falls back to "
          "transactions, so its timing differs from the unobserved link",
          name());
    }
    trans.set_dmi_allowed(false);
    return false;
  }

  unsigned int transport_dbg(tlm::tlm_generic_payload &trans) {
    return to->transport_dbg(trans);
  }

  /**
   * @brief Report one transport call and pass it on.
   *
   * Reports the called phase before passing the call on, so a call the
   * receiver makes back through this link is reported after it, then a
   * TLM_UPDATED or TLM_COMPLETED return. A managed payload is held across the
   * call because the receiver may release it before returning.
   *
   * @param trans Payload of the call.
   * @param phase The caller's phase; holds the returned phase after @p call.
   * @param delay The caller's delay; holds the returned delay after @p call.
   * @param call Calls the receiver and returns its status.
   * @return The receiver's status, unchanged.
   */
  template <typename Call>
  tlm::tlm_sync_enum Pass(tlm::tlm_generic_payload &trans,
                          const tlm::tlm_phase &phase, const sc_time &delay,
                          Call call) {
    const tlm::tlm_phase called = phase;
    Report(trans, called, std::nullopt, delay);
    const bool managed = trans.has_mm();
    if (managed) trans.acquire();
    const tlm::tlm_sync_enum status = call();
    if (status == tlm::TLM_UPDATED) {
      Report(trans, phase, status, delay);
    } else if (status == tlm::TLM_COMPLETED) {
      Report(trans, called, status, delay);
    }
    if (managed) trans.release();
    return status;
  }

  /** @brief Pass one call or return to the observer at its annotated time. */
  void Report(tlm::tlm_generic_payload &trans, const tlm::tlm_phase &phase,
              std::optional<tlm::tlm_sync_enum> status, const sc_time &delay) {
    if (delay != SC_ZERO_TIME && !warned_delay_) {
      warned_delay_ = true;
      LV_WARNING(
          "{}: a call crossed the link with an annotated delay of {}; it is "
          "passed on unchanged, but the delay reaches observers only as their "
          "time argument",
          name(), delay.to_string());
    }
    observer_(trans, phase, status, sc_time_stamp() + delay);
  }

  Observer observer_;
  Target *target_ = nullptr;
  bool warned_delay_ = false;
  bool warned_dmi_ = false;
};

}  // namespace dbg
