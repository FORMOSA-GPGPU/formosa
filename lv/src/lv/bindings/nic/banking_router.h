/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/schema.h>
#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/multi_passthrough_initiator_socket.h>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <tlm_utils/peq_with_get.h>

#include <memory>
#include <unordered_map>
#include <vector>

namespace nic {

// A # of M master port -> # of N slave port interconnect
class BankingRouter : public sc_module {
 public:
  struct Param {
    uint64_t num_froms = 4;
    uint64_t total_size = 1024;
    uint64_t num_tos = 4;
    uint64_t bank_line_size = 64;
    // clang-format off
    LV_SCHEMA(BankingRouter, Param,
              LV_FIELD(num_froms, "Number of master (from) ports"),
              LV_FIELD(total_size, "Total address space size in bytes"),
              LV_FIELD(num_tos, "Number of bank (to) ports"),
              LV_FIELD(bank_line_size, "Bank interleave granularity in bytes"))
    // clang-format on
  };
  using TargetSocket =
      tlm_utils::multi_passthrough_target_socket<BankingRouter>;
  using InitiatorSocket =
      tlm_utils::multi_passthrough_initiator_socket<BankingRouter>;

  TargetSocket from;
  InitiatorSocket to;

  BankingRouter(const sc_core::sc_module_name &name, const Param &param);
  ~BankingRouter() override = default;

  /* Lua property setter: each assignment binds the next bank target. */
  void set_to(InitiatorSocket::base_target_socket_type *target) {
    to.bind(*target);
  }

  uint64_t num_banks() const { return num_banks_; }
  uint64_t total_size() const { return total_size_; }

 private:
  struct from_to_info_t {
    uint64_t m_id;
    uint64_t s_id;
    uint64_t orig_addr;
  };

  uint64_t total_size_;
  uint64_t num_froms_;
  uint64_t num_banks_;
  uint64_t bank_line_size_;

  /* Internal request buffer */
  tlm_utils::peq_with_get<tlm::tlm_generic_payload> peq_fw_;
  tlm_utils::peq_with_get<tlm::tlm_generic_payload> peq_bw_;
  std::vector<std::unique_ptr<tlm::tlm_fifo<tlm::tlm_generic_payload *>>>
      req_q_;
  std::vector<std::unique_ptr<tlm::tlm_fifo<tlm::tlm_generic_payload *>>>
      resp_q_;

  /* Map to route responses back to the correct master index */
  std::unordered_map<tlm::tlm_generic_payload *, from_to_info_t> trans_mas_sla_;

  std::vector<std::unique_ptr<sc_core::sc_event>> req_end_event_;
  std::vector<std::unique_ptr<sc_core::sc_event>> resp_end_event_;

  /* TLM-2.0 non-blocking transport callbacks */
  tlm::tlm_sync_enum nb_transport_fw(int m_id, tlm::tlm_generic_payload &trans,
                                     tlm::tlm_phase &phase,
                                     sc_core::sc_time &delay);
  tlm::tlm_sync_enum nb_transport_bw(int s_id, tlm::tlm_generic_payload &trans,
                                     tlm::tlm_phase &phase,
                                     sc_core::sc_time &delay);

  /* Process that handles transactions on the clock edge */
  void HandleRequest();
  void ForwardRequest(size_t s_id);

  void HandleResponse();
  void ForwardResponse(size_t m_id);

  uint64_t ToBankId(uint64_t addr) const;
  uint64_t ToBankLocalAddress(uint64_t addr) const;

  /* Debug transport callback */
  unsigned int transport_dbg(int id, tlm::tlm_generic_payload &trans);
};

}  // namespace nic
