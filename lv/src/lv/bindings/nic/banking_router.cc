// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#define SC_INCLUDE_DYNAMIC_PROCESSES

#include "banking_router.h"

#include <fmt/format.h>
#include <liblv/binding.h>
#include <liblv/output.h>

namespace nic {

BankingRouter::BankingRouter(const sc_core::sc_module_name &name,
                             const Param &param)
    : sc_module(name),
      from("from"),
      to("to"),
      total_size_(param.total_size),
      num_froms_(param.num_froms),
      num_banks_(param.num_tos),
      bank_line_size_(param.bank_line_size),
      peq_fw_("peq_fw"),
      peq_bw_("peq_bw") {
  if (num_banks_ == 0) {
    lv::Fatal("BankingRouter requires at least one bank");
  }
  if (num_froms_ == 0) {
    lv::Fatal("BankingRouter requires at least one master port");
  }
  if (total_size_ == 0) {
    lv::Fatal("BankingRouter requires a non-zero address space");
  }
  if (bank_line_size_ == 0) {
    lv::Fatal("BankingRouter requires a non-zero bank line size");
  }

  for (uint64_t i = 0; i < param.num_tos; ++i) {
    req_q_.emplace_back(
        std::make_unique<tlm::tlm_fifo<tlm::tlm_generic_payload *>>());
    req_end_event_.emplace_back(std::make_unique<sc_core::sc_event>());
  }

  for (uint64_t i = 0; i < param.num_froms; ++i) {
    resp_q_.emplace_back(
        std::make_unique<tlm::tlm_fifo<tlm::tlm_generic_payload *>>());
    resp_end_event_.emplace_back(std::make_unique<sc_core::sc_event>());
  }

  from.register_nb_transport_fw(this, &BankingRouter::nb_transport_fw);
  from.register_transport_dbg(this, &BankingRouter::transport_dbg);
  to.register_nb_transport_bw(this, &BankingRouter::nb_transport_bw);

  SC_THREAD(HandleRequest);
  for (uint64_t i = 0; i < param.num_tos; i++) {
    sc_spawn(sc_bind(&BankingRouter::ForwardRequest, this, i),
             sc_gen_unique_name("ForwardRequest"));
  }

  SC_THREAD(HandleResponse);
  for (uint64_t i = 0; i < param.num_froms; i++) {
    sc_spawn(sc_bind(&BankingRouter::ForwardResponse, this, i),
             sc_gen_unique_name("ForwardResponse"));
  }
}

tlm::tlm_sync_enum BankingRouter::nb_transport_fw(
    int m_id, tlm::tlm_generic_payload &trans, tlm::tlm_phase &phase,
    sc_core::sc_time &delay) {
  switch (phase) {
    case tlm::BEGIN_REQ:
      trans_mas_sla_[&trans] = {static_cast<uint64_t>(m_id), 0, 0};
      peq_fw_.notify(trans);
      return tlm::TLM_ACCEPTED;
    case tlm::END_RESP:
      resp_end_event_[m_id]->notify();
      return tlm::TLM_COMPLETED;
    default:
      SC_REPORT_FATAL("BankingRouter", "Illegal transaction phase received");
      return tlm::TLM_COMPLETED;
  }
}

void BankingRouter::HandleRequest() {
  while (true) {
    wait(peq_fw_.get_event());

    tlm::tlm_generic_payload *trans = nullptr;
    while ((trans = peq_fw_.get_next_transaction()) != nullptr) {
      trans->acquire();

      // Get target address and length.
      auto addr = trans->get_address();
      auto len = trans->get_data_length();

      // Check if the request is valid.
      if (addr >= total_size_ || len == 0 || len > total_size_ - addr) {
        lv::Fatal("Out-of-range request for address={}, data_len={}", addr,
                  len);
      }
      const auto line_offset = addr % bank_line_size_;
      if (len > bank_line_size_ - line_offset) {
        lv::Fatal("Request crosses bank line for address={}, data_len={}", addr,
                  len);
      }

      // Record original address.
      trans_mas_sla_[trans].orig_addr = addr;

      // Get Bank ID and bank-local address.
      auto s_id = ToBankId(addr);
      auto local_addr = ToBankLocalAddress(addr);

      // Update address to the bank-local address.
      trans->set_address(local_addr);

      auto m_id = trans_mas_sla_[trans].m_id;
      trans_mas_sla_[trans].s_id = s_id;
      req_q_[s_id]->put(trans);

      tlm::tlm_phase phase = tlm::END_REQ;
      sc_time delay = SC_ZERO_TIME;
      from[m_id]->nb_transport_bw(*trans, phase, delay);
    }
  }
}

void BankingRouter::ForwardRequest(size_t s_id) {
  for (;;) {
    tlm::tlm_generic_payload *trans = req_q_[s_id]->get();

    tlm::tlm_phase phase = tlm::BEGIN_REQ;
    sc_time delay = SC_ZERO_TIME;
    to[s_id]->nb_transport_fw(*trans, phase, delay);
    wait(*req_end_event_[s_id]);
    trans->release();
  }
}

tlm::tlm_sync_enum BankingRouter::nb_transport_bw(
    int s_id, tlm::tlm_generic_payload &trans, tlm::tlm_phase &phase,
    sc_core::sc_time &delay) {
  switch (phase) {
    case tlm::END_REQ:
      req_end_event_[s_id]->notify();
      return tlm::TLM_UPDATED;
    case tlm::BEGIN_RESP:
      peq_bw_.notify(trans);
      return tlm::TLM_ACCEPTED;
    default:
      SC_REPORT_FATAL("BankingRouter", "Illegal transaction phase received");
      return tlm::TLM_COMPLETED;
  }
}

void BankingRouter::HandleResponse() {
  while (true) {
    wait(peq_bw_.get_event());

    tlm::tlm_generic_payload *trans = nullptr;
    while ((trans = peq_bw_.get_next_transaction()) != nullptr) {
      trans->acquire();
      auto m_id = trans_mas_sla_[trans].m_id;
      auto s_id = trans_mas_sla_[trans].s_id;

      resp_q_[m_id]->put(trans);

      // Reply the response when the transaction is really put to resp_q_
      tlm::tlm_phase phase = tlm::END_RESP;
      sc_time delay = SC_ZERO_TIME;
      to[s_id]->nb_transport_fw(*trans, phase, delay);
    }
  }
}

void BankingRouter::ForwardResponse(size_t m_id) {
  while (true) {
    tlm::tlm_generic_payload *trans = resp_q_[m_id]->get();

    tlm::tlm_phase phase = tlm::BEGIN_RESP;
    sc_time delay = SC_ZERO_TIME;
    trans->set_address(trans_mas_sla_[trans].orig_addr);
    from[m_id]->nb_transport_bw(*trans, phase, delay);
    wait(*resp_end_event_[m_id]);
    trans_mas_sla_.erase(trans);
    trans->release();
  }
}

unsigned int BankingRouter::transport_dbg(int id,
                                          tlm::tlm_generic_payload &trans) {
  auto address = trans.get_address();
  auto bank_id = ToBankId(address);
  auto phy_addr = ToBankLocalAddress(address);
  trans.set_address(phy_addr);
  unsigned int result = to[bank_id]->transport_dbg(trans);
  trans.set_address(address);
  return result;
}

uint64_t BankingRouter::ToBankId(uint64_t addr) const {
  return (addr / bank_line_size_) % num_banks_;
}

uint64_t BankingRouter::ToBankLocalAddress(uint64_t addr) const {
  return (addr / (bank_line_size_ * num_banks_)) * bank_line_size_ +
         (addr % bank_line_size_);
}

LV_BINDING(nic, BankingRouter)
    .constructor(
        [](const char *name, const BankingRouter::Param &param) {
          return std::make_shared<BankingRouter>(name, param);
        },
        lv::params("name", "param"), lv::doc("Create a banking router"))
    .property("from", &BankingRouter::from,
              lv::doc("Multi-target input socket"))
    .property("to", &BankingRouter::set_to,
              lv::doc("Bind the next bank target"));

}  // namespace nic
