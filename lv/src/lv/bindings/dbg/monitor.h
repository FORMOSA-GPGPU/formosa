/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

namespace dbg {

class Monitor : public sc_module {
 public:
  using Target =
      tlm_utils::simple_initiator_socket<Monitor>::base_target_socket_type;

  explicit Monitor(const sc_module_name &name);
  ~Monitor() = default;

  void set_to(Target *target);
  Target *to() const;

  tlm_utils::simple_target_socket<Monitor> from_;
  tlm_utils::simple_initiator_socket<Monitor> to_;

 protected:
  virtual tlm::tlm_sync_enum nb_transport_fw(tlm::tlm_generic_payload &trans,
                                             tlm::tlm_phase &phase, sc_time &);
  virtual tlm::tlm_sync_enum nb_transport_bw(tlm::tlm_generic_payload &trans,
                                             tlm::tlm_phase &phase, sc_time &);

  virtual unsigned int transport_dbg(tlm::tlm_generic_payload &trans);
  void PrintPayload(tlm::tlm_generic_payload &trans, tlm::tlm_phase &phase,
                    sc_time &delay);
  Target *target_;
};

}  // namespace dbg
