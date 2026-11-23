/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/common/tlm_sink.h>
#include <liblv/common/tlm_source.h>
#include <liblv/mm/static.h>
#include <systemc.h>
#include <tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h>

#include <deque>
#include <memory>
#include <queue>
#include <vector>

#include "cores/scalar/fetch_entry.h"
#include "cores/scalar/packet.h"
#include "konata/konata.h"

namespace simtix::scalar {

class ScalarCore : public sc_module {
 public:
  sc_in<bool> SC_NAMED(clock);
  sc_in<bool> SC_NAMED(ext_int);
  sc_in<bool> SC_NAMED(sw_int);
  sc_in<bool> SC_NAMED(timer_int);

  ScalarCore(const sc_module_name &name)
      : packet_pool_(10),
        imem_port_("imem_port"),
        dmem_port_("dmem_port"),
        dummy_tmask_(1) {
    dmem_trans_.set_mm(lv::mm::Static);
    fetch_entries_.reserve(kNumFetchEntries);
    for (uint32_t i = 0; i < kNumFetchEntries; ++i) {
      fetch_entries_.push_back(std::make_unique<FetchEntry>());
      free_fetch_entries_.push(fetch_entries_.back().get());
    }
    SC_METHOD(Tick);
    sensitive << clock.pos();

    // Scalar core executes one active lane.
    dummy_tmask_[0] = true;
  }

  virtual ~ScalarCore() = default;

  // Lua APIs
  void set_clock(sc_clock *clock) { this->clock.bind(*clock); }
  void set_imem(lv::TlmSource::Target *target) {
    imem_port_.set_target(target);
  }
  void set_dmem(lv::TlmSource::Target *target) {
    dmem_port_.set_target(target);
  }
  void set_pc(uint64_t pc) { pc_ = pc; }

  // Konata tracing
  void enable_konata_trace(const std::string &path);
  void disable_konata_trace();

  void Tick();

 private:
  static constexpr uint32_t kNumFetchEntries = 4;

  void before_end_of_elaboration() override;
  uint64_t PendingInterrupts() const;
  bool ReadCsr(uint32_t addr, uint64_t &value) const;
  void WriteCsr(uint32_t addr, uint64_t value);
  bool ExecuteCsr(Packet *packet);
  void EnterTrap(uint64_t cause, uint64_t epc, uint64_t tval,
                 bool interrupt = false);
  void Redirect(uint64_t pc);
  void Discard(Packet *&packet);

  void Fetch();
  void CollectFetchResponses();
  void DeliverFetchedInstruction();
  void IssueFetchRequest();
  void InvalidateOutstandingFetches();
  void Decode();
  void Execute();
  void Memory();
  void Writeback();

  void UpdatePipelineRegisters();

  template <typename RetT, typename CastT>
  void SignExtensionImpl(Packet *p) {
    assert(p);
    p->data_buf = static_cast<RetT>(*reinterpret_cast<CastT *>(&p->data_buf));
  }

  template <typename T, typename... Rest>
  void DispatchSignExtension(Packet *p, uint32_t size, bool is_signed) {
    if (sizeof(T) == size) {
      if (is_signed) {
        SignExtensionImpl<int64_t, std::make_signed_t<T>>(p);
      } else {
        SignExtensionImpl<uint64_t, std::make_unsigned_t<T>>(p);
      }
    } else if constexpr (sizeof...(Rest) > 0) {
      DispatchSignExtension<Rest...>(p, size, is_signed);
    }
  }

  void SignExtension(Packet *p, uint32_t size, bool is_signed) {
    DispatchSignExtension<int8_t, int16_t, int32_t, int64_t>(p, size,
                                                             is_signed);
  }

  void SetupDmemTrans(Packet *p, tlm::tlm_command command, uint32_t len) {
    dmem_trans_.set_command(command);
    dmem_trans_.set_address(p->addr_buf);
    dmem_trans_.set_data_length(len);
    dmem_trans_.set_streaming_width(len);
    dmem_trans_.set_data_ptr(reinterpret_cast<unsigned char *>(&p->data_buf));
    dmem_trans_.set_byte_enable_length(0);
    dmem_trans_.set_byte_enable_ptr(nullptr);
    dmem_trans_.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
  }

  PacketPool packet_pool_;
  uint64_t pc_ = 0;
  std::array<int64_t, 32> regfile_ = {0};

  // This core implements RV64 machine mode only. MPP is consequently fixed
  // to M, and only the machine interrupt enable stack is writable.
  uint64_t mstatus_ = MSTATUS_MPP;
  uint64_t mie_ = 0;
  uint64_t mtvec_ = 0;
  uint64_t mscratch_ = 0;
  uint64_t mepc_ = 0;
  uint64_t mcause_ = 0;
  uint64_t mtval_ = 0;

  sc_signal<bool> SC_NAMED(inactive_interrupt_, false);

  lv::TlmSource imem_port_;
  lv::TlmSource dmem_port_;

  tlm::tlm_generic_payload dmem_trans_;

  std::vector<std::unique_ptr<FetchEntry>> fetch_entries_;
  std::queue<FetchEntry *> free_fetch_entries_;
  std::deque<FetchEntry *> issued_fetch_entries_;
  uint64_t imem_epoch_ = 0;

  Packet *packet_f_ = nullptr;
  Packet *packet_d_ = nullptr;
  Packet *packet_e_ = nullptr;
  Packet *packet_m_ = nullptr;
  Packet *packet_w_ = nullptr;

  std::unique_ptr<konata::KonataTracer<Packet>> konata_tracer_;

  // Control signals
  bool flush_ = false;
  bool stall_ = false;

  bool dmem_busy_ = false;

  // Dummy variable for ExecContext
  sc_bv_base dummy_tmask_;
};
}  // namespace simtix::scalar
