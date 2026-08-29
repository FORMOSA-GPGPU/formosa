// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/pipelined/arbitrator/simple.h"

#include <liblv/binding.h>

namespace simtix::pipelined {

void SimpleArbitrator::HandleOperandCollectReq() {
  while (operand_collect_req->nb_can_get() &&
         operand_collect_resp->nb_can_put()) {
    Packet *packet = operand_collect_req->get();
    if (packet->instr.rs1() != Instr::kNullReg) {
      core_->ReadRegFile(packet->rs1_data.data(), packet->wid,
                         packet->instr.rs1());
    }
    if (packet->instr.rs2() != Instr::kNullReg) {
      core_->ReadRegFile(packet->rs2_data.data(), packet->wid,
                         packet->instr.rs2());
    }
    if (packet->instr.rs3() != Instr::kNullReg) {
      core_->ReadRegFile(packet->rs3_data.data(), packet->wid,
                         packet->instr.rs3());
    }
    if (scoreboard_) scoreboard_->RegReadDone(packet);
    operand_collect_resp->put(packet);
  }
  next_trigger(operand_collect_req->ok_to_get() |
               operand_collect_resp->ok_to_put());
}

void SimpleArbitrator::HandleWritebackReq() {
  while (writeback_req->nb_can_get() && writeback_resp->nb_can_put()) {
    Packet *packet = writeback_req->get();
    if (packet->instr.rd() != Instr::kNullReg && packet->instr.rd() != 0) {
      core_->WriteRegFile(packet->data_buf.data(), packet->wid,
                          packet->instr.rd(), packet->tmask);
    }
    writeback_resp->put(packet);
  }
  next_trigger(writeback_req->ok_to_get() | writeback_resp->ok_to_put());
}

LV_BINDING_WITH_BASES(simtix, SimpleArbitrator, ArbitratorIntf)
    .constructor(
        [](const char *name, const ArchParam &param) {
          return std::make_shared<SimpleArbitrator>(name, param);
        },
        lv::params("name", "param"), lv::doc("Create a simple arbitrator"));

}  // namespace simtix::pipelined
