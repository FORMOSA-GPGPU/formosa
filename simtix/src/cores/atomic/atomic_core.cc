// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/atomic/atomic_core.h"

#include <liblv/binding.h>
#include <liblv/output.h>
#include <tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h>

#include "cores/decode.h"
#include "cores/encoding.h"
#include "cores/exec_context.h"
#include "cores/exec_flag.h"
#include "cores/formosa.h"
#include "cores/instr.h"

namespace simtix {

namespace {

/**
 * Decode mtval for traps observed by the atomic core.
 *
 * - ECALL reports zero.
 * - Illegal instructions report the raw instruction word.
 * - Instruction-address misalignment reports the first active lane's misaligned
 * target address.
 * - Load/store address misalignment reports the first active lane's
 *   misaligned effective address.
 */
uint64_t DecodeTrapVal(ExecFlag flag, uint32_t iword, const sc_bv_base &tmask,
                       const std::vector<uint64_t> &addr_buf,
                       uint32_t num_lanes) {
  switch (GetException(flag)) {
    case ExecFlag::ECALL:
      return 0;
    case ExecFlag::ILLEGAL:
      return iword;
    case ExecFlag::INSTR_ADDR_MISALIGNED:
      for (uint32_t i = 0; i < num_lanes; ++i) {
        if (tmask[i] == 1 && addr_buf[i] % 4 != 0) {
          return addr_buf[i];
        }
      }
      return 0;
    case ExecFlag::LOAD_ADDR_MISALIGNED:
    case ExecFlag::STORE_ADDR_MISALIGNED: {
      uint32_t alignment = GetMemSize(flag);
      for (uint32_t i = 0; i < num_lanes; ++i) {
        if (tmask[i] == 1 && addr_buf[i] % alignment != 0) {
          return addr_buf[i];
        }
      }
      return 0;
    }
    default:
      return 0;
  }
}

}  // namespace

void AtomicCore::MainProc() {
  cmd_ready_ = true;
  while (true) {
    // Wait until we have active warps
    while (active_mask_.val() == 0) {
      wait(active_mask_.value_changed_event());
    }

    if (!sched_) {
      assert(0);  // scheduler is not set!
    }

    // Warp scheduling, wpc arbitration and tmask generation
    uint32_t wid = sched_->SelectWarp(active_mask_);
    wmask_ = 1 << wid;

    uint64_t wpc = ArbitratePC(wid);
    current_pc_ = wpc;
    UpdateThreadMask(wid, wpc);

    // Issue the memory request to fetch the instruction
    uint32_t iword = Fetch(wpc);
    AdvancePC(wid);

    // Decode
    Instr instr = Decode(iword);

    ExecContext ctx = {
        .pc = wpc,
        .tmask = tmask_,
        .rs1_data = ReadRegfile(wid, instr.rs1()),
        .rs2_data = ReadRegfile(wid, instr.rs2()),
        .rs3_data = ReadRegfile(wid, instr.rs3()),
        .dynamic_rm = &ptfrm_[wid * num_lanes_],
        .num_lanes = num_lanes_,
        .rd_data = data_buf_.data(),
        .next_pc = addr_buf_.data(),
        .mem = {.addr = addr_buf_.data(), .data = data_buf_.data()},
        .csr = {.addr = &csr_buf_, .data = data_buf_.data()},
        .pri = &pri_buf_,
        .fflags = fflags_buf_.data(),
    };

    // Execute
    ExecFlag flag = Execute(&ctx, instr);

    // WGI may abort this warp while the atomic instruction is in flight.
    // Consume the result without committing architectural state or performing
    // a stale warp-state transition after the warp is no longer active.
    if (active_mask_.val()[wid] == 0) {
      continue;
    }

    if (HasFlag(flag, ExecFlag::RD_DATA)) {
      WriteRegfile(wid, instr.rd());
      stats_.committed_arithmetic_instr[wid] +=
          !HasFlag(flag, ExecFlag::NEXT_PC);
    }

    if (HasFlag(flag, ExecFlag::NEXT_PC)) {
      UpdatePC(wid);
      bool is_uncond_jump = HasFlag(flag, ExecFlag::RD_DATA);
      stats_.committed_cond_branch_instr[wid] += !is_uncond_jump;
      stats_.committed_uncond_jump_instr[wid] += is_uncond_jump;
    }

    if (HasFlag(flag, ExecFlag::LOAD)) {
      SetupTrans(tlm::TLM_READ_COMMAND, GetMemSize(flag));
      start_issuing_mem_req_.notify();
      start_collecting_mem_resp_.notify();
      wait(done_issuing_mem_req_ & done_collecting_mem_resp_);
      SignExtension(GetMemSize(flag), IsSigned(flag));
      WriteRegfile(wid, instr.rd());
      stats_.committed_load_instr[wid]++;
    }

    if (HasFlag(flag, ExecFlag::ATOMIC)) {
      ExecuteAtomicInstr(GetMemSize(flag), IsSigned(flag),
                         DecodeAtomicOp(flag));
      WriteRegfile(wid, instr.rd());
    }

    if (HasFlag(flag, ExecFlag::STORE)) {
      SetupTrans(tlm::TLM_WRITE_COMMAND, GetMemSize(flag));
      start_issuing_mem_req_.notify();
      start_collecting_mem_resp_.notify();
      wait(done_issuing_mem_req_ & done_collecting_mem_resp_);
      stats_.committed_store_instr[wid]++;
    }

    if (HasFlag(flag, ExecFlag::CSR)) {
      ExecuteCsrOp(wid, GetCsrOp(flag));
      WriteRegfile(wid, instr.rd());
      stats_.committed_system_instr[wid]++;
    }

    // Accrue floating point exceptions
    if (HasFlag(flag, ExecFlag::FP_EXC)) {
      UpdateFflags(wid);
      stats_.committed_fp_instr[wid]++;
    }

    if (HasFlag(flag, ExecFlag::PRIORITY)) {
      UpdatePri(wid, GetPriOp(flag));
      stats_.committed_custom_instr[wid]++;
    }

    if (HasFlag(flag, ExecFlag::BARRIER)) {
      pending_barrier_tmask_[wid] |= tmask_;

      if (pending_barrier_tmask_[wid].and_reduce()) {
        WarpStateTransition(&barrier_mask_, &active_mask_, wmask_);
        pending_barrier_tmask_[wid] = 0;
      }
      stats_.committed_custom_instr[wid]++;
    }

    if (HasFlag(flag, ExecFlag::ECALL)) {
      pending_ecall_tmask_[wid] |= tmask_;

      if (pending_ecall_tmask_[wid].and_reduce()) {
        mcause_[wid] = DecodeExceptionCause(flag);
        mepc_[wid] = wpc;
        mtval_[wid] = 0;
        WarpStateTransition(&exception_mask_, &active_mask_, wmask_);
        pending_ecall_tmask_[wid] = 0;
      }
      stats_.committed_system_instr[wid]++;
    }

    if (HasException(flag) && !HasFlag(flag, ExecFlag::ECALL)) {
      mcause_[wid] = DecodeExceptionCause(flag);
      mepc_[wid] = wpc;
      mtval_[wid] = DecodeTrapVal(flag, iword, tmask_, addr_buf_, num_lanes_);
      WarpStateTransition(&exception_mask_, &active_mask_, wmask_);
    }

    // Done
    minstret_[wid]++;
    stats_.warp_instret[wid]++;
    for (uint32_t i = 0; i < num_lanes_; ++i) {
      if (tmask_[i]) {
        stats_.lane_instret[i]++;
      }
    }
  }
}

void AtomicCore::IssueMemReq() {
  for (;;) {
    wait(start_issuing_mem_req_);
    for (uint32_t i = 0; i < num_lanes_; ++i) {
      if (tmask_[i] == 1) {
        mem_port_.req_port->write(&trans_[i]);
      }
    }
    done_issuing_mem_req_.notify();
  }
}

void AtomicCore::CollectMemResp() {
  for (;;) {
    wait(start_collecting_mem_resp_);
    for (uint32_t i = 0; i < num_lanes_; ++i) {
      if (tmask_[i] == 1) {
        auto *resp = mem_port_.resp_port->read();
        assert(resp->is_response_ok());
      }
    }
    done_collecting_mem_resp_.notify();
  }
}

void AtomicCore::ExecuteWarpCtrlCommand(
    const lv::formosa::WarpCtrlCommand &cmd) {
  switch (cmd.op()) {
    case lv::formosa::WarpCtrlCommand::Op::kActivate:
      Activate(cmd.cwm(), cmd.pc(), cmd.wg_info(), cmd.cwid_base());
      break;
    case lv::formosa::WarpCtrlCommand::Op::kResume:
      Resume(cmd.cwm());
      break;
    case lv::formosa::WarpCtrlCommand::Op::kRelease:
      Release(cmd.cwm());
      break;
    case lv::formosa::WarpCtrlCommand::Op::kAbort:
      Abort(cmd.cwm());
      break;
  }
}

void AtomicCore::Activate(const sc_dt::sc_bv_base &cwm, uint64_t pc,
                          uint64_t wg_info, uint64_t cwid_base) {
  assert(cmd_ready_);
  uint32_t active_warp_cnt = 0;
  for (int w = 0; w < num_warps_; ++w) {
    if (cwm[w].to_bool()) {
      // Setup the PC of all threads in the warp
      for (int l = 0; l < num_lanes_; ++l) {
        ptpc_[w * num_lanes_ + l] = pc;
        ptpri_[w * num_lanes_ + l] = 0;
      }

      // Setup the WGInfo
      mscratch_[w] = wg_info;

      // Setup the CWID
      cwid_[w] = cwid_base + active_warp_cnt;

      ++active_warp_cnt;
    }
  }
  WarpStateTransition(&active_mask_, &idle_mask_, cwm);
}

void AtomicCore::Resume(const sc_dt::sc_bv_base &cwm) {
  assert(cmd_ready_);
  WarpStateTransition(&active_mask_, &barrier_mask_, cwm);
}

void AtomicCore::Release(const sc_dt::sc_bv_base &cwm) {
  assert(cmd_ready_);
  WarpStateTransition(&idle_mask_, &exception_mask_, cwm);
}

void AtomicCore::Abort(const sc_dt::sc_bv_base &cwm) {
  assert(cmd_ready_);
  active_mask_ = active_mask_.val() & ~cwm;
  barrier_mask_ = barrier_mask_.val() & ~cwm;
  idle_mask_ = idle_mask_.val() | cwm;
}

uint64_t AtomicCore::ArbitratePC(uint32_t wid) {
  bool found = false;
  uint32_t lane_id = 0;
  uint8_t max_pri = 0;
  uint8_t *pwpri = &ptpri_[wid * num_lanes_];
  for (uint32_t i = 0; i < num_lanes_; ++i) {
    /* Skip this thread if it already reaches ecall state. */
    if (pending_ecall_tmask_[wid][i] == 1 ||
        pending_barrier_tmask_[wid][i] == 1) {
      continue;
    }

    if (!found || pwpri[i] > max_pri) {
      found = true;
      lane_id = i;
      max_pri = pwpri[i];
    }
  }
  assert(found);
  return ptpc_[wid * num_lanes_ + lane_id];
}

void AtomicCore::UpdateThreadMask(uint32_t wid, uint64_t wpc) {
  uint64_t *pwpc = &ptpc_[wid * num_lanes_];
  for (uint32_t i = 0; i < num_lanes_; ++i) {
    tmask_[i] = pwpc[i] == wpc;
  }
}

uint32_t AtomicCore::Fetch(uint64_t pc) {
  constexpr uint32_t kLane = 0; /* Use lane 0 to fetch the instruction. */
  addr_buf_[kLane] = pc;
  SetupTrans(kLane, tlm::TLM_READ_COMMAND, 4);
  mem_port_.req_port->write(&trans_[kLane]);
  auto *resp = mem_port_.resp_port->read();
  assert(resp->is_response_ok());
  uint32_t iword = *reinterpret_cast<uint32_t *>(&data_buf_[kLane]);
  return iword;
}

void AtomicCore::SetupTrans(uint32_t lane_id, tlm::tlm_command command,
                            uint32_t len) {
  auto &trans = trans_[lane_id];
  trans.clear_extension<AtomicExtension>();
  trans.set_command(command);
  trans.set_address(addr_buf_[lane_id]);
  trans.set_data_length(len);
  trans.set_data_ptr(reinterpret_cast<unsigned char *>(&data_buf_[lane_id]));
  trans.set_byte_enable_ptr(nullptr);
  trans.set_byte_enable_length(0);
  trans.set_streaming_width(len);
  trans.set_dmi_allowed(false);
  trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
  trans.set_extension(exts_[lane_id].get());
  exts_[lane_id]->ip = current_pc_;
}

void AtomicCore::SetupAtomicTrans(uint32_t lane_id, uint32_t len,
                                  AtomicExtension::Op op) {
  SetupTrans(lane_id, tlm::TLM_READ_COMMAND, len);
  atomic_exts_[lane_id]->op = op;
  trans_[lane_id].set_extension(atomic_exts_[lane_id].get());
}

void AtomicCore::SetupTrans(tlm::tlm_command command, uint32_t len) {
  for (uint32_t i = 0; i < num_lanes_; ++i) {
    if (tmask_[i] == 1) {
      SetupTrans(i, command, len);
    }
  }
}

void AtomicCore::SetupAtomicTrans(uint32_t len, AtomicExtension::Op op) {
  for (uint32_t i = 0; i < num_lanes_; ++i) {
    if (tmask_[i] == 1) {
      SetupAtomicTrans(i, len, op);
    }
  }
}

void AtomicCore::ExecuteAtomicInstr(uint32_t len, bool is_signed,
                                    AtomicExtension::Op op) {
  for (uint32_t i = 0; i < num_lanes_; ++i) {
    if (tmask_[i] == 0) {
      continue;
    }

    SetupAtomicTrans(i, len, op);
    mem_port_.req_port->write(&trans_[i]);
    auto *resp = mem_port_.resp_port->read();
    assert(resp == &trans_[i]);
    assert(resp->is_response_ok());
  }

  SignExtension(len, is_signed);
}

int64_t *AtomicCore::ReadRegfile(uint32_t wid, uint8_t rs) {
  if (rs == Instr::kNullReg) {
    return nullptr;
  } else {
    return &regfile_[ToRegfileIndex(wid, rs)];
  }
}

void AtomicCore::SignExtension(uint32_t size, bool is_signed) {
  DispatchSignExtension<int8_t, int16_t, int32_t, int64_t>(size, is_signed);
}

void AtomicCore::WriteRegfile(uint32_t wid, uint8_t rd) {
  if (rd == Instr::kNullReg || rd == 0) {
    return;
  }

  int64_t *wreg = &regfile_[ToRegfileIndex(wid, rd)];
  for (uint32_t i = 0; i < num_lanes_; ++i) {
    if (tmask_[i] == 1) {
      wreg[i] = data_buf_[i];
    }
  }
}

void AtomicCore::AdvancePC(uint32_t wid) {
  uint64_t *pwpc = &ptpc_[wid * num_lanes_];
  for (uint32_t i = 0; i < num_lanes_; ++i) {
    if (tmask_[i] == 1) {
      pwpc[i] += 4;
    }
  }
}

void AtomicCore::UpdatePC(uint32_t wid) {
  uint64_t *pwpc = &ptpc_[wid * num_lanes_];
  for (uint32_t i = 0; i < num_lanes_; ++i) {
    if (tmask_[i] == 1) {
      pwpc[i] = addr_buf_[i];
    }
  }
}

void AtomicCore::UpdatePri(uint32_t wid, ExecFlag op) {
  int8_t factor = op != ExecFlag::PRI_RESET;
  int8_t sign = op == ExecFlag::PRI_RAISE ? 1 : -1;

  uint8_t *pwpri = &ptpri_[wid * num_lanes_];
  for (uint32_t i = 0; i < num_lanes_; ++i) {
    if (tmask_[i] == 1) {
      pwpri[i] = factor * (pwpri[i] + sign * pri_buf_);
    }
  }
}

void AtomicCore::UpdateFflags(uint32_t wid) {
  uint8_t *pwfflags = &ptfflags_[wid * num_lanes_];
  for (uint32_t i = 0; i < num_lanes_; ++i) {
    if (tmask_[i] == 1) {
      pwfflags[i] |= fflags_buf_[i] & 0x1f;
    }
  }
}

void AtomicCore::ExecuteCsrOp(uint32_t wid, ExecFlag op) {
  for (uint32_t i = 0; i < num_lanes_; ++i) {
    if (tmask_[i] == 1) {
      uint64_t old_val = 0;
      uint64_t mask = data_buf_[i];

      auto apply_op = [&](uint64_t current) {
        if (op == ExecFlag::CSRRW) return mask;
        if (op == ExecFlag::CSRRS) return current | mask;
        if (op == ExecFlag::CSRRC) return current & ~mask;
        return current;
      };

      switch (csr_buf_) {
        case CSR_CYCLE:
        case CSR_MCYCLE:
          old_val = mcycle();
          break;
        case CSR_MHARTID:
          old_val = cwid_[wid] * num_lanes_ + i;
          break;
        case CSR_MSCRATCH:
          old_val = mscratch_[wid];
          mscratch_[wid] = apply_op(old_val);
          break;
        case CSR_MCAUSE:
          old_val = mcause_[wid];
          mcause_[wid] = apply_op(old_val);
          break;
        case CSR_MEPC:
          old_val = mepc_[wid];
          mepc_[wid] = apply_op(old_val);
          break;
        case CSR_MTVAL:
          old_val = mtval_[wid];
          mtval_[wid] = apply_op(old_val);
          break;
        case CSR_MINSTRET:
          old_val = minstret_[wid];
          minstret_[wid] = apply_op(old_val);
          break;
        case CSR_FFLAGS:
          old_val = ptfflags_[wid * num_lanes_ + i] & 0x1f;
          ptfflags_[wid * num_lanes_ + i] = apply_op(old_val) & 0x1f;
          break;
        case CSR_FRM:
          old_val = ptfrm_[wid * num_lanes_ + i] & 0x7;
          ptfrm_[wid * num_lanes_ + i] = apply_op(old_val) & 0x7;
          break;
        case CSR_FCSR: {
          old_val = (ptfrm_[wid * num_lanes_ + i] << 5) |
                    (ptfflags_[wid * num_lanes_ + i] & 0x1f);
          uint64_t new_val = apply_op(old_val);
          ptfrm_[wid * num_lanes_ + i] = (new_val >> 5) & 0x7;
          ptfflags_[wid * num_lanes_ + i] = new_val & 0x1f;
          break;
        }
        case CSR_XWID:
          old_val = wid;
          break;
        case CSR_XLANEID:
          old_val = i;
          break;
        case CSR_XLANES:
          old_val = num_lanes_;
          break;
        default:
          LV_FATAL("Unsupported CSR address: {:#x}", csr_buf_);
          break;
      }
      data_buf_[i] = old_val;
    }
  }
}

LV_BINDING(simtix, AtomicCore)
    .constructor(
        [](const char *name, const ArchParam &param) {
          return std::make_shared<AtomicCore>(name, param);
        },
        lv::params("name", "param"), lv::doc("Create an atomic core"))
    .property("clock", &BaseCore::set_clock, lv::doc("SystemC clock"))
    .property("target", &AtomicCore::target, &AtomicCore::set_target,
              lv::doc("Instruction and data memory target"))
    .method("sched_init", &AtomicCore::sched_init,
            lv::params(
                lv::param("sched_init",
                          lv::lua_type("fun(name: string): simtix.WarpSched"))),
            lv::doc("Register the scheduler factory"))
    .property("warp_ctrl", &AtomicCore::warp_ctrl,
              lv::doc("Warp-control interface"))
    .property("stats", &AtomicCore::stats, lv::doc("Statistics group"));

}  // namespace simtix
