/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/simple_target_socket.h>

#include <optional>

#include "pass_through.h"

namespace dbg {

/** @brief Logs each phase and TLM_COMPLETED return that crosses a TLM link. */
class Monitor : public sc_module {
 public:
  /** @brief Target socket type that the outgoing socket binds to. */
  using Target = PassThrough::Target;

  /**
   * @brief Create a monitor.
   *
   * @param name Module name.
   */
  explicit Monitor(const sc_module_name &name);

  /** @brief Return the incoming socket that initiators bind to. */
  tlm_utils::simple_target_socket<PassThrough> *from() { return &link_.from; }

  /**
   * @brief Bind the outgoing socket to a target.
   *
   * @param target Target socket that receives the passed-on calls.
   */
  void set_to(Target *target) { link_.bind(target); }

  /**
   * @brief Return the target given to set_to().
   *
   * @return The bound target, or nullptr before set_to().
   */
  Target *to() const { return link_.target(); }

 private:
  void PrintPayload(const tlm::tlm_generic_payload &trans,
                    const tlm::tlm_phase &phase,
                    std::optional<tlm::tlm_sync_enum> status);

  PassThrough link_;
};

}  // namespace dbg
