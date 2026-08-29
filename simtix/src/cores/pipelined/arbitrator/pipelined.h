/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/trace.h>
#include <systemc.h>

#include <queue>
#include <vector>

#include "cores/encoding.h"
#include "cores/pipelined/arbitrator/base.h"
#include "cores/pipelined/arbitrator/scalar_regfile.h"

namespace simtix::pipelined {

class PipelinedArbitrator : public ArbitratorIntf {
 public:
  enum class RFArch : uint8_t { kBaseline, kDuplicateSRF };

  struct Param {
    RFArch rf_arch = RFArch::kBaseline;

    uint32_t num_read_collect_units = 4;
    uint32_t num_write_collect_units = 1;
    uint32_t num_regfile_banks = 4;
    uint32_t num_subcores = 1;

    // Default to 1RW config
    uint32_t num_shared_ports = 1;
    uint32_t num_read_ports = 0;
    uint32_t num_write_ports = 0;

    // Default perfetto enable off
    bool pftrace = false;

    // clang-format off
    LV_SCHEMA(PipelinedArbitrator, Param,
              LV_FIELD_ENUM(rf_arch, "Regfile architecture",
                            {
                                {RFArch::kBaseline, "Baseline"},
                                {RFArch::kDuplicateSRF, "DuplicateSRF"},
                            }),
              LV_FIELD(num_read_collect_units,
                       "Number of read collect units per subcore"),
              LV_FIELD(num_write_collect_units,
                       "Number of write collect units per subcore"),
              LV_FIELD(num_regfile_banks, "Number of register file banks"),
              LV_FIELD(num_subcores, "Number of subcores per SM"),
              LV_FIELD(num_shared_ports,
                       "Number of shared register file ports"),
              LV_FIELD(num_read_ports, "Number of read register file ports"),
              LV_FIELD(num_write_ports, "Number of write register file ports"),
              LV_FIELD(pftrace, "Whether to enable Perfetto trace or not"))
    // clang-format on
  };

  explicit PipelinedArbitrator(const sc_module_name &name, const ArchParam &p,
                               const Param &pp);

  lv::stats::Group *stats() const override { return &stats_; }

 private:
  void AllocateReadCollectUnit();
  void AllocateWriteCollectUnit();

  void ArbitrateRequests();

  void ForwardReadPacket();
  void ForwardWritePacket();

  void Tick();

  enum class OpPosition : uint8_t {
    kRd = 0,
    kRs1,
    kRs2,
    kRs3,
  };

  static std::string_view OpPositionToString(OpPosition op_pos) {
    constexpr static const char *kName[] = {"Rd", "Rs1", "Rs2", "Rs3"};
    return kName[static_cast<uint8_t>(op_pos)];
  }

  static uint8_t OpPositionToRegId(OpPosition op_pos, Packet *packet) {
    switch (op_pos) {
      case OpPosition::kRd:
        return packet->instr.rd();
      case OpPosition::kRs1:
        return packet->instr.rs1();
      case OpPosition::kRs2:
        return packet->instr.rs2();
      case OpPosition::kRs3:
        return packet->instr.rs3();
      default:
        return -1;
    }
  }

  struct Request {
    uint32_t cu_id;
    uint32_t wid;
    const sc_bv_base *tmask;
    int64_t *data_ptr;
    uint32_t req_bank;
    OpPosition op_pos;
    uint8_t reg_id;
    bool is_write;

    Request(uint32_t cu_id, uint32_t wid, const sc_bv_base *tmask,
            int64_t *data_ptr, uint32_t req_bank, OpPosition op_pos,
            uint8_t reg_id, bool is_write)
        : cu_id(cu_id),
          wid(wid),
          tmask(tmask),
          data_ptr(data_ptr),
          req_bank(req_bank),
          op_pos(op_pos),
          reg_id(reg_id),
          is_write(is_write) {}
  };

  class ReadCollectUnit {
   public:
    bool IsBusy() const { return packet_ != nullptr; }

    bool IsReady() const {
      return packet_ != nullptr && rs1_ready_ && rs2_ready_ && rs3_ready_;
    }

    bool IsScalarizable() const {
      assert(IsReady());

      // 1. Every lane in the warp must be active.
      if (!packet_->tmask.and_reduce()) {
        return false;
      }

      // 2. The current instruction must be non-divergent and
      //    does not support load/store instructions in the scalar pipeline.
      if (packet_->instr.is_cti() || packet_->instr.is_mem()) {
        return false;
      }

      // 3. If every source operand is uniform, the instruction can use the
      //    scalar pipeline.
      bool all_uniform = (IsAbsent(rs1_kind_) || IsUniform(rs1_kind_)) &&
                         (IsAbsent(rs2_kind_) || IsUniform(rs2_kind_)) &&
                         (IsAbsent(rs3_kind_) || IsUniform(rs3_kind_));

      if (all_uniform) {
        return true;
      }

      // 4. Otherwise, only allow the affine add cases:
      //    - ADD: exactly one affine source operand, with the other source
      //    uniform.
      //    - ADDI: rs1 is affine and the immediate is treated as the uniform
      //    second operand.
      uint32_t iword = packet_->iword;
      bool is_add = (iword & MASK_ADD) == MATCH_ADD;
      bool is_addi = (iword & MASK_ADDI) == MATCH_ADDI;

      if (is_add) {
        return (IsUniform(rs1_kind_) && IsAffine(rs2_kind_)) ||
               (IsAffine(rs1_kind_) && IsUniform(rs2_kind_));
      }

      if (is_addi) {
        return IsAffine(rs1_kind_);
      }

      return false;
    }

    void SetReady(OpPosition op_pos) {
      switch (op_pos) {
        case OpPosition::kRs1:
          rs1_ready_ = true;
          break;
        case OpPosition::kRs2:
          rs2_ready_ = true;
          break;
        case OpPosition::kRs3:
          rs3_ready_ = true;
          break;
        default:
          break;
      }
    }

    void SetSourceKind(OpPosition op_pos, SRFValueKind kind) {
      switch (op_pos) {
        case OpPosition::kRs1:
          rs1_kind_ = kind;
          break;
        case OpPosition::kRs2:
          rs2_kind_ = kind;
          break;
        case OpPosition::kRs3:
          rs3_kind_ = kind;
          break;
        default:
          break;
      }
    }

    void Allocate(Packet *pkt) {
      packet_ = pkt;

      if (pkt->instr.rs1() == 0) {
        std::fill(pkt->rs1_data.begin(), pkt->rs1_data.end(), 0);
      }
      if (pkt->instr.rs2() == 0) {
        std::fill(pkt->rs2_data.begin(), pkt->rs2_data.end(), 0);
      }
      if (pkt->instr.rs3() == 0) {
        std::fill(pkt->rs3_data.begin(), pkt->rs3_data.end(), 0);
      }
      rs1_ready_ = IsAbsentOrZeroReg(pkt->instr.rs1());
      rs2_ready_ = IsAbsentOrZeroReg(pkt->instr.rs2());
      rs3_ready_ = IsAbsentOrZeroReg(pkt->instr.rs3());

      rs1_kind_ = InitialSourceKind(pkt->instr.rs1());
      rs2_kind_ = InitialSourceKind(pkt->instr.rs2());
      rs3_kind_ = InitialSourceKind(pkt->instr.rs3());
    }

    void Deallocate() {
      packet_ = nullptr;
      rs1_ready_ = false;
      rs2_ready_ = false;
      rs3_ready_ = false;
      rs1_kind_ = SRFValueKind::kUnknown;
      rs2_kind_ = SRFValueKind::kUnknown;
      rs3_kind_ = SRFValueKind::kUnknown;
    }

    Packet *packet() { return packet_; }
    bool rs1_ready() const { return rs1_ready_; }
    bool rs2_ready() const { return rs2_ready_; }
    bool rs3_ready() const { return rs3_ready_; }

   private:
    static bool IsAbsent(SRFValueKind kind) {
      return kind == SRFValueKind::kAbsent;
    }

    static bool IsUniform(SRFValueKind kind) {
      return kind == SRFValueKind::kUniform;
    }

    static bool IsAffine(SRFValueKind kind) {
      return kind == SRFValueKind::kAffine;
    }

    static bool IsAbsentOrZeroReg(uint8_t reg_id) {
      return reg_id == Instr::kNullReg || reg_id == 0;
    }

    static SRFValueKind InitialSourceKind(uint8_t reg_id) {
      if (reg_id == Instr::kNullReg) {
        return SRFValueKind::kAbsent;
      }
      if (reg_id == 0) {
        return SRFValueKind::kUniform;
      }
      return SRFValueKind::kUnknown;
    }

    Packet *packet_ = nullptr;
    bool rs1_ready_ = false;
    bool rs2_ready_ = false;
    bool rs3_ready_ = false;
    SRFValueKind rs1_kind_ = SRFValueKind::kUnknown;
    SRFValueKind rs2_kind_ = SRFValueKind::kUnknown;
    SRFValueKind rs3_kind_ = SRFValueKind::kUnknown;
  };

  class WriteCollectUnit {
   public:
    bool IsBusy() const { return packet_ != nullptr; }
    bool IsReady() const { return packet_ != nullptr && rd_ready_; }
    void SetReady(OpPosition op_pos) {
      switch (op_pos) {
        case OpPosition::kRd:
          rd_ready_ = true;
          break;
        default:
          break;
      }
    }

    void Allocate(Packet *pkt) {
      packet_ = pkt;
      rd_ready_ = pkt->instr.rd() == Instr::kNullReg || pkt->instr.rd() == 0;
    }

    void Deallocate() {
      packet_ = nullptr;
      rd_ready_ = false;
    }

    Packet *packet() { return packet_; }
    bool rd_ready() const { return rd_ready_; }

   private:
    Packet *packet_ = nullptr;
    bool rd_ready_ = false;
  };

  uint32_t ToRegfileBank(uint32_t wid, uint32_t reg_id) const;
  uint32_t local_wid(uint32_t wid) const { return wid / num_subcores_; }
  void AccessRegfile(const Request &req);
  std::tuple<bool, int64_t, int64_t> AffineDetection(
      const std::vector<int64_t> &data_buf, const sc_bv_base &tmask);

  const uint32_t num_lanes_;

  const RFArch rf_arch_;
  const uint32_t num_regfile_banks_;
  const uint32_t num_subcores_;
  const size_t num_shared_ports_;
  const size_t num_read_ports_;
  const size_t num_write_ports_;

  using RequestBuffer = std::vector<Request>;
  using BankAccessCount = std::vector<size_t>;

  std::vector<RequestBuffer *> req_buf_arr_;
  std::array<RequestBuffer, 2> req_buf_;  // {S, ?} or {W, R}
  RequestBuffer *read_req_buf_;
  RequestBuffer *write_req_buf_;

  std::vector<BankAccessCount *> bank_access_cnt_arr_;
  std::array<BankAccessCount, 2> bank_access_cnt_;  // {S, ?} or {W, R}
  BankAccessCount *read_bank_access_cnt_;
  BankAccessCount *write_bank_access_cnt_;

  std::queue<uint32_t> free_read_collect_unit_ids_;
  std::queue<uint32_t> free_write_collect_unit_ids_;
  std::vector<uint32_t> working_read_collect_unit_ids_;
  std::vector<uint32_t> working_write_collect_unit_ids_;
  std::vector<ReadCollectUnit> read_collect_units_;
  std::vector<WriteCollectUnit> write_collect_units_;

  std::vector<uint32_t> port_utilized_;
  std::unique_ptr<ScalarRegFile> scalar_regfile_;

  struct Stats : lv::stats::Group {
    Metric rf_read_reqs;
    Metric rf_write_reqs;
    Formula<Integer> rf_total_reqs;
    Metric rf_read_conflicts;
    Metric rf_write_conflicts;
    Metric rf_total_bank_conflicts;
    Formula<Real> rf_read_conflict_ratio;
    Formula<Real> rf_write_conflict_ratio;
    Formula<Real> rf_total_conflict_ratio;
    Metric rf_total_available_banks;
    Formula<Real> rf_bank_utilization;
    Metric operand_collected_instrs;
    Metric scalarizable_instrs;
    Formula<Real> scalarizable_instr_ratio;

    Stats(const char *name)
        : Group(name),
          LV_STAT(rf_read_reqs, "Number of regfile read requests"),
          LV_STAT(rf_write_reqs, "Number of regfile write requests"),
          LV_STAT(rf_total_reqs, "Number of total regfile requests"),
          LV_STAT(rf_read_conflicts, "Number of regfile read conflicts"),
          LV_STAT(rf_write_conflicts, "Number of regfile write conflicts"),
          LV_STAT(rf_total_bank_conflicts, "Number of total regfile conflicts"),
          LV_STAT(rf_read_conflict_ratio,
                  "Ratio of bank-conflicted regfile read requests"),
          LV_STAT(rf_write_conflict_ratio,
                  "Ratio of bank-conflicted regfile write requests"),
          LV_STAT(rf_total_conflict_ratio,
                  "Ratio of bank-conflicted total regfile requests"),
          LV_STAT(rf_total_available_banks,
                  "Number of total available regfile banks"),
          LV_STAT(rf_bank_utilization, "Regfile bank utilization rate"),
          LV_STAT(operand_collected_instrs,
                  "Number of operand-collected instructions"),
          LV_STAT(scalarizable_instrs,
                  "Number of instructions executable by the scalar unit"),
          LV_STAT(scalarizable_instr_ratio,
                  "Ratio of instructions executable by the scalar unit") {
      rf_total_reqs = rf_read_reqs + rf_write_reqs;
      rf_read_conflict_ratio = rf_read_conflicts / rf_read_reqs;
      rf_write_conflict_ratio = rf_write_conflicts / rf_write_reqs;
      rf_total_conflict_ratio = rf_total_bank_conflicts / rf_total_reqs;
      rf_bank_utilization = rf_total_reqs / rf_total_available_banks;
      scalarizable_instr_ratio = scalarizable_instrs / operand_collected_instrs;
    }
  } mutable stats_;

  // Perfetto Tracing
  std::vector<lv::trace::Track> rcu_tracks_;
  std::vector<lv::trace::Track> wcu_tracks_;
  std::vector<lv::trace::Track> bank_conflict_tracks_;
};
}  // namespace simtix::pipelined
