// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/pipelined/lsu/simple.h"

#include <liblv/binding.h>

#include "cores/exec_flag.h"

namespace simtix::pipelined {

void SimpleLsu::SetupTrans(tlm::tlm_command command, uint32_t len) {
  assert(packet_ != nullptr);
  for (uint32_t i = 0; i < num_lanes_; ++i) {
    if (packet_->tmask[i] == 1) {
      auto &lane_trans = trans_[i];
      lane_trans.clear_extension<AtomicExtension>();
      lane_trans.set_command(command);
      lane_trans.set_address(packet_->addr_buf[i]);
      lane_trans.set_data_length(len);
      lane_trans.set_data_ptr(
          reinterpret_cast<unsigned char *>(&packet_->data_buf[i]));
      lane_trans.set_byte_enable_ptr(nullptr);
      lane_trans.set_byte_enable_length(0);
      lane_trans.set_streaming_width(len);
      lane_trans.set_dmi_allowed(false);
      lane_trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
      lane_trans.set_extension(exts_[i].get());
      exts_[i]->ip = packet_->wpc;
    }
  }
}

void SimpleLsu::SetupAtomicTrans(uint32_t len, AtomicExtension::Op op) {
  SetupTrans(tlm::TLM_READ_COMMAND, len);
  SetupAtomicExtensions(packet_->tmask, atomic_exts_, trans_, op);
}

void SimpleLsu::HandleProc() {
  for (;;) {
    packet_ = lsu_req->get();
    auto flag = packet_->flag;

    if (HasFlag(flag, ExecFlag::ATOMIC)) {
      SetupAtomicTrans(GetMemSize(flag), simtix::DecodeAtomicOp(flag));
      start_issuing_mem_req_.notify();
      start_collecting_mem_resp_.notify();
      wait(done_issuing_mem_req_ & done_collecting_mem_resp_);
      SignExtension(GetMemSize(flag), IsSigned(flag));
    }

    if (HasFlag(flag, ExecFlag::LOAD)) {
      SetupTrans(tlm::TLM_READ_COMMAND, GetMemSize(flag));
      start_issuing_mem_req_.notify();
      start_collecting_mem_resp_.notify();
      wait(done_issuing_mem_req_ & done_collecting_mem_resp_);
      SignExtension(GetMemSize(flag), IsSigned(flag));
    }

    if (HasFlag(flag, ExecFlag::STORE)) {
      SetupTrans(tlm::TLM_WRITE_COMMAND, GetMemSize(flag));
      start_issuing_mem_req_.notify();
      start_collecting_mem_resp_.notify();
      wait(done_issuing_mem_req_ & done_collecting_mem_resp_);
    }

    lsu_resp->put(packet_);
  }
}

void SimpleLsu::IssueMemReq() {
  for (;;) {
    wait(start_issuing_mem_req_);
    for (uint32_t i = 0; i < num_lanes_; ++i) {
      if (packet_->tmask[i] == 1) {
        dmem_port_.req_port->write(&trans_[i]);
      }
    }
    done_issuing_mem_req_.notify();
  }
}

void SimpleLsu::CollectMemResp() {
  for (;;) {
    wait(start_collecting_mem_resp_);
    for (uint32_t i = 0; i < num_lanes_; ++i) {
      if (packet_->tmask[i] == 1) {
        auto *resp = dmem_port_.resp_port->read();
        assert(resp->is_response_ok());
      }
    }
    done_collecting_mem_resp_.notify();
  }
}

LV_BINDING_WITH_BASES(simtix, SimpleLsu, Lsu)
    .constructor(
        [](const char *name, const ArchParam &param) {
          return std::make_shared<SimpleLsu>(name, param);
        },
        lv::params("name", "param"), lv::doc("Create a simple LSU"));

}  // namespace simtix::pipelined
