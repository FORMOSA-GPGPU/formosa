/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/axi/axi_helpers.h>
#include <liblv/axi/axi_signals.h>
#include <liblv/common/tlm_source.h>
#include <liblv/log.h>
#include <liblv/mm/pool.h>
#include <systemc.h>
#include <tlm.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <ostream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace lv::axi {

namespace detail {

class AxiResponseOrder {
 public:
  using Payload = tlm::tlm_generic_payload;

  void Register(uint32_t txn_id, Payload *payload);

  void Complete(Payload *payload);

  Payload *TakeReady();

 private:
  std::unordered_map<uint32_t, std::deque<Payload *>> pending_;
  std::unordered_set<Payload *> completed_;
};

}  // namespace detail

/**
 * AXI pin-level slave adapter that emits TLM initiator transactions.
 *
 * Use this adapter when an RTL AXI master needs to access a TLM-modeled memory,
 * bus, or device. The adapter accepts AXI AR/AW/W handshakes and forwards the
 * resulting generic payloads through an internal TlmSource.
 * Only aligned, complete beats are supported; unaligned beats and non-INCR
 * bursts terminate the simulation with LV_FATAL.
 */
template <size_t AddrW, size_t DataW, size_t IdW>
class AxiInitiatorAdapter : public sc_core::sc_module {
 public:
  static_assert(AddrW > 0 && AddrW <= 32, "AXI address width must be 1..32");
  static_assert(IdW > 0 && IdW <= 32, "AXI ID width must be 1..32");
  static_assert(DataW == 8 || DataW == 16 || DataW == 32 || DataW == 64,
                "AXI data width must be 8, 16, 32 or 64");

  using Target = lv::TlmSource::Target;

  /**
   * Constructs the adapter.
   *
   * @param name SystemC module name.
   * @param config Adapter behavior and default response settings.
   */
  explicit AxiInitiatorAdapter(const sc_core::sc_module_name &name,
                               AxiAdapterConfig config = {})
      : sc_core::sc_module(name),
        clk_i_("clk_i"),
        rst_ni_("rst_ni"),
        ar_valid("ar_valid"),
        ar_ready("ar_ready"),
        ar_id("ar_id"),
        ar_addr("ar_addr"),
        ar_len("ar_len"),
        ar_size("ar_size"),
        ar_burst("ar_burst"),
        r_valid("r_valid"),
        r_ready("r_ready"),
        r_id("r_id"),
        r_data("r_data"),
        r_resp("r_resp"),
        r_last("r_last"),
        aw_valid("aw_valid"),
        aw_ready("aw_ready"),
        aw_id("aw_id"),
        aw_addr("aw_addr"),
        aw_len("aw_len"),
        aw_size("aw_size"),
        aw_burst("aw_burst"),
        w_valid("w_valid"),
        w_ready("w_ready"),
        w_data("w_data"),
        w_strb("w_strb"),
        w_last("w_last"),
        b_valid("b_valid"),
        b_ready("b_ready"),
        b_id("b_id"),
        b_resp("b_resp"),
        source_("source", 16),
        write_addr_fifo_("write_addr_fifo", 16),
        write_data_fifo_("write_data_fifo", 256),
        config_(config),
        read_beat_gap_cycles_(EnvUInt("LV_AXI_READ_BEAT_GAP", 0)),
        read_burst_gap_cycles_(EnvUInt("LV_AXI_READ_BURST_GAP", 0)),
        write_response_gap_cycles_(EnvUInt("LV_AXI_WRITE_RESP_GAP", 0)) {
    SC_THREAD(ReadRequestThread);
    SC_THREAD(WriteAddressThread);
    SC_THREAD(WriteDataThread);
    SC_THREAD(WriteIssueThread);
    SC_THREAD(CompletionThread);
    SC_THREAD(ReadResponseThread);
    SC_THREAD(WriteResponseThread);
    SC_THREAD(ResetMonitorThread);
  }

  /** Binds the adapter clock input. */
  void bind_clock(sc_core::sc_signal_in_if<bool> &clk_if) {
    clk_i_.bind(clk_if);
  }
  /** Binds the active-low reset input. */
  void bind_reset_n(sc_core::sc_signal_in_if<bool> &rst_if) {
    rst_ni_.bind(rst_if);
  }

  /** Binds all AXI pins from an AxiSignals bundle. */
  void bind_pins(const AxiSignals<AddrW, DataW, IdW> &pins) {
    ar_valid.bind(*pins.ar_valid);
    ar_ready.bind(*pins.ar_ready);
    ar_id.bind(*pins.ar_id);
    ar_addr.bind(*pins.ar_addr);
    ar_len.bind(*pins.ar_len);
    ar_size.bind(*pins.ar_size);
    ar_burst.bind(*pins.ar_burst);
    r_valid.bind(*pins.r_valid);
    r_ready.bind(*pins.r_ready);
    r_id.bind(*pins.r_id);
    r_data.bind(*pins.r_data);
    r_resp.bind(*pins.r_resp);
    r_last.bind(*pins.r_last);
    aw_valid.bind(*pins.aw_valid);
    aw_ready.bind(*pins.aw_ready);
    aw_id.bind(*pins.aw_id);
    aw_addr.bind(*pins.aw_addr);
    aw_len.bind(*pins.aw_len);
    aw_size.bind(*pins.aw_size);
    aw_burst.bind(*pins.aw_burst);
    w_valid.bind(*pins.w_valid);
    w_ready.bind(*pins.w_ready);
    w_data.bind(*pins.w_data);
    w_strb.bind(*pins.w_strb);
    w_last.bind(*pins.w_last);
    b_valid.bind(*pins.b_valid);
    b_ready.bind(*pins.b_ready);
    if (pins.b_id != nullptr) {
      b_id.bind(*pins.b_id);
    } else {
      b_id.bind(dummy_b_id_);
    }
    b_resp.bind(*pins.b_resp);
  }

  /** Connects the outgoing TLM initiator path to a target socket. */
  void set_target(Target *target) {
    target_ = target;
    source_.set_target(target_);
  }

  /** Returns the currently connected TLM target socket. */
  [[nodiscard]] Target *target() const { return target_; }

  // Clock and active-low reset.
  sc_core::sc_in<bool> clk_i_;
  sc_core::sc_in<bool> rst_ni_;

  // Read address channel consumed by this adapter.
  sc_core::sc_in<bool> ar_valid;
  sc_core::sc_out<bool> ar_ready;
  sc_core::sc_in<uint32_t> ar_id;
  sc_core::sc_in<uint32_t> ar_addr;
  sc_core::sc_in<uint32_t> ar_len;
  sc_core::sc_in<uint32_t> ar_size;
  sc_core::sc_in<uint32_t> ar_burst;

  // Read data channel driven by this adapter.
  sc_core::sc_out<bool> r_valid;
  sc_core::sc_in<bool> r_ready;
  sc_core::sc_out<uint32_t> r_id;
  sc_core::sc_out<uint64_t> r_data;
  sc_core::sc_out<uint32_t> r_resp;
  sc_core::sc_out<bool> r_last;

  // Write address channel consumed by this adapter.
  sc_core::sc_in<bool> aw_valid;
  sc_core::sc_out<bool> aw_ready;
  sc_core::sc_in<uint32_t> aw_id;
  sc_core::sc_in<uint32_t> aw_addr;
  sc_core::sc_in<uint32_t> aw_len;
  sc_core::sc_in<uint32_t> aw_size;
  sc_core::sc_in<uint32_t> aw_burst;

  // Write data channel consumed by this adapter.
  sc_core::sc_in<bool> w_valid;
  sc_core::sc_out<bool> w_ready;
  sc_core::sc_in<uint64_t> w_data;
  sc_core::sc_in<uint32_t> w_strb;
  sc_core::sc_in<bool> w_last;

  // Write response channel driven by this adapter.
  sc_core::sc_out<bool> b_valid;
  sc_core::sc_in<bool> b_ready;
  sc_core::sc_out<uint32_t> b_id;
  sc_core::sc_out<uint32_t> b_resp;

 private:
  static constexpr size_t kBusBytes = DataW / 8;

  struct PendingRead {
    /// AXI metadata captured from the AR channel.
    AxiExtension ext;
    uint64_t reset_generation = 0;
    /// Data buffer owned until the TLM read response returns.
    std::unique_ptr<uint8_t[]> data;
  };

  struct PendingWriteAddress {
    /// AXI metadata captured from the AW channel.
    AxiExtension ext;
    uint64_t base_addr = 0;
    size_t total_bytes = 0;
    uint64_t reset_generation = 0;

    friend std::ostream &operator<<(std::ostream &os,
                                    const PendingWriteAddress &addr) {
      return os << "{addr=0x" << std::hex << addr.base_addr << std::dec
                << ", bytes=" << addr.total_bytes << "}";
    }
  };

  struct WriteBeat {
    uint64_t data = 0;
    uint32_t strobe = 0;
    bool last = false;
    uint64_t reset_generation = 0;

    friend std::ostream &operator<<(std::ostream &os, const WriteBeat &beat) {
      return os << "{data=0x" << std::hex << beat.data << ", strobe=0x"
                << beat.strobe << std::dec << ", last=" << beat.last << "}";
    }
  };

  struct PendingWrite {
    /// AXI metadata captured from the AW channel.
    AxiExtension ext;
    uint64_t reset_generation = 0;
    /// Data buffer assembled from WDATA beats.
    std::unique_ptr<uint8_t[]> data;
    /// TLM byte-enable buffer assembled from WSTRB.
    std::unique_ptr<uint8_t[]> byte_enable;
  };

  sc_core::sc_signal<uint32_t> dummy_b_id_{"dummy_b_id"};
  Target *target_ = nullptr;
  lv::TlmSource source_;
  sc_core::sc_fifo<PendingWriteAddress> write_addr_fifo_;
  sc_core::sc_fifo<WriteBeat> write_data_fifo_;
  AxiAdapterConfig config_;
  uint32_t read_beat_gap_cycles_ = 0;
  uint32_t read_burst_gap_cycles_ = 0;
  uint32_t write_response_gap_cycles_ = 0;
  bool transaction_active_ = false;
  uint64_t reset_generation_ = 0;
  bool reset_active_ = false;
  sc_core::sc_event transaction_released_;
  std::unordered_map<tlm::tlm_generic_payload *, PendingRead> pending_reads_;
  std::unordered_map<tlm::tlm_generic_payload *, PendingWrite> pending_writes_;
  detail::AxiResponseOrder read_response_order_;
  detail::AxiResponseOrder write_response_order_;
  sc_core::sc_event read_completion_event_;
  sc_core::sc_event write_completion_event_;

  void ResetOutputs() {
    ar_ready.write(false);
    r_valid.write(false);
    r_id.write(0);
    r_data.write(0);
    r_resp.write(kOkayResp);
    r_last.write(false);
    aw_ready.write(false);
    w_ready.write(false);
    b_valid.write(false);
    b_id.write(0);
    b_resp.write(kOkayResp);
  }

  void ReadRequestThread() {
    for (;;) {
      WaitForReady();
      while (rst_ni_.read() && !ar_valid.read()) {
        wait(clk_i_.posedge_event());
      }
      if (!rst_ni_.read()) {
        continue;
      }
      uint64_t generation = 0;
      if (!AcquireTransactionSlot(generation)) continue;

      // VALID may have dropped after the previous handshake while we waited.
      if (!ar_valid.read()) {
        ReleaseTransactionSlot(generation);
        continue;
      }

      ar_ready.write(true);
      do {
        wait(clk_i_.posedge_event());
      } while (rst_ni_.read() && !ar_valid.read());
      if (!rst_ni_.read()) {
        ar_ready.write(false);
        ReleaseTransactionSlot(generation);
        continue;
      }

      const auto base_addr = static_cast<uint64_t>(ar_addr.read());
      // Check full-width pin values before narrowing metadata or shifting.
      const auto total_bytes = ValidateGeometry(
          "read", base_addr, ar_len.read(), ar_size.read(), ar_burst.read());
      AxiExtension ext;
      ext.txn_id = ar_id.read();
      ext.burst_len = ar_len.read();
      ext.beat_size = static_cast<uint8_t>(ar_size.read());
      ext.burst_type = static_cast<uint8_t>(ar_burst.read());
      ext.axi_resp = config_.default_read_resp;
      ar_ready.write(false);
      LV_DEBUG("{} AR addr={:#x} bytes={} id={} beats={} size={}", name(),
               base_addr, total_bytes, ext.txn_id, ext.NumBeats(),
               ext.beat_size);

      auto *trans = lv::mm::Pool::Allocate();
      trans->acquire();
      auto *ext_copy = new AxiExtension(ext);
      trans->set_extension(ext_copy);

      PendingRead pending;
      pending.ext = ext;
      pending.reset_generation = generation;
      pending.data = std::make_unique<uint8_t[]>(total_bytes);
      std::memset(pending.data.get(), 0, total_bytes);

      trans->set_command(tlm::TLM_READ_COMMAND);
      trans->set_address(base_addr);
      trans->set_data_ptr(pending.data.get());
      trans->set_data_length(total_bytes);
      trans->set_streaming_width(total_bytes);
      trans->set_byte_enable_ptr(nullptr);
      trans->set_byte_enable_length(0);
      trans->set_dmi_allowed(false);
      trans->set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

      pending_reads_.emplace(trans, std::move(pending));
      read_response_order_.Register(ext.txn_id, trans);
      // The response will be routed back through CompletionThread.
      source_.req_port->write(trans);
    }
  }

  void WriteAddressThread() {
    for (;;) {
      WaitForReady();
      while (rst_ni_.read() && !aw_valid.read()) {
        wait(clk_i_.posedge_event());
      }
      if (!rst_ni_.read()) {
        continue;
      }
      uint64_t generation = 0;
      if (!AcquireTransactionSlot(generation)) continue;

      if (!aw_valid.read()) {
        ReleaseTransactionSlot(generation);
        continue;
      }

      aw_ready.write(true);
      do {
        wait(clk_i_.posedge_event());
      } while (rst_ni_.read() && !aw_valid.read());
      if (!rst_ni_.read()) {
        aw_ready.write(false);
        ReleaseTransactionSlot(generation);
        continue;
      }

      const auto base_addr = static_cast<uint64_t>(aw_addr.read());
      // Check full-width pin values before narrowing metadata or shifting.
      const auto total_bytes = ValidateGeometry(
          "write", base_addr, aw_len.read(), aw_size.read(), aw_burst.read());
      AxiExtension ext;
      ext.txn_id = aw_id.read();
      ext.burst_len = aw_len.read();
      ext.beat_size = static_cast<uint8_t>(aw_size.read());
      ext.burst_type = static_cast<uint8_t>(aw_burst.read());
      ext.axi_resp = config_.default_write_resp;
      aw_ready.write(false);
      LV_DEBUG("{} AW addr={:#x} bytes={} id={} beats={} size={}", name(),
               base_addr, total_bytes, ext.txn_id, ext.NumBeats(),
               ext.beat_size);

      PendingWriteAddress pending_address{ext, base_addr, total_bytes,
                                          generation};
      while (rst_ni_.read() && !write_addr_fifo_.nb_write(pending_address)) {
        wait(clk_i_.posedge_event());
      }
      if (!rst_ni_.read()) {
        ReleaseTransactionSlot(generation);
      }
    }
  }

  void WriteDataThread() {
    for (;;) {
      WaitForReady();
      while (rst_ni_.read()) {
        const bool can_accept = write_data_fifo_.num_free() > 0;
        w_ready.write(can_accept);
        wait(clk_i_.posedge_event());
        if (!rst_ni_.read()) {
          break;
        }
        if (can_accept && w_valid.read()) {
          write_data_fifo_.nb_write(
              WriteBeat{w_data.read(), static_cast<uint32_t>(w_strb.read()),
                        w_last.read(), reset_generation_});
        }
      }
      w_ready.write(false);
    }
  }

  void WriteIssueThread() {
    for (;;) {
      WaitForReady();
      PendingWriteAddress aw;
      if (!WaitForWriteAddress(aw) ||
          aw.reset_generation != reset_generation_) {
        continue;
      }

      const auto beat_bytes = SizeCodeToBytes(aw.ext.beat_size);
      const auto total_beats = aw.ext.NumBeats();

      PendingWrite pending;
      pending.ext = aw.ext;
      pending.reset_generation = aw.reset_generation;
      pending.data = std::make_unique<uint8_t[]>(aw.total_bytes);
      pending.byte_enable = std::make_unique<uint8_t[]>(aw.total_bytes);
      std::memset(pending.data.get(), 0, aw.total_bytes);
      std::memset(pending.byte_enable.get(), 0, aw.total_bytes);

      bool write_aborted = false;
      for (size_t beat = 0; beat < total_beats; ++beat) {
        WriteBeat write_beat;
        if (!WaitForWriteBeat(write_beat) ||
            write_beat.reset_generation != aw.reset_generation) {
          write_aborted = true;
          break;
        }

        if (write_beat.last != (beat + 1 == total_beats)) {
          LV_FATAL(
              "{} AXI->TLM write: WLAST does not match AWLEN "
              "(id={}, beat={}, beats={}, last={})",
              name(), aw.ext.txn_id, beat, total_beats, write_beat.last);
        }
        const auto beat_addr =
            BeatAddress(aw.base_addr, beat, beat_bytes, aw.ext.burst_type);
        LV_TRACE("{} W beat={} addr={:#x} data={:#x} strb={:#x} last={}",
                 name(), beat, beat_addr, write_beat.data, write_beat.strobe,
                 write_beat.last);
        ExtractWriteBeat(beat_addr, write_beat.data, write_beat.strobe,
                         pending.data.get() + beat * beat_bytes,
                         pending.byte_enable.get() + beat * beat_bytes,
                         beat_bytes, kBusBytes);
      }
      if (write_aborted) {
        ReleaseTransactionSlot(aw.reset_generation);
        continue;
      }

      auto *trans = lv::mm::Pool::Allocate();
      trans->acquire();
      auto *ext_copy = new AxiExtension(aw.ext);
      trans->set_extension(ext_copy);
      trans->set_command(tlm::TLM_WRITE_COMMAND);
      trans->set_address(aw.base_addr);
      trans->set_data_ptr(pending.data.get());
      trans->set_data_length(aw.total_bytes);
      trans->set_streaming_width(aw.total_bytes);
      trans->set_byte_enable_ptr(pending.byte_enable.get());
      trans->set_byte_enable_length(aw.total_bytes);
      trans->set_dmi_allowed(false);
      trans->set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

      pending_writes_.emplace(trans, std::move(pending));
      write_response_order_.Register(aw.ext.txn_id, trans);
      // The response will be converted into the AXI B channel later.
      source_.req_port->write(trans);
    }
  }

  void CompletionThread() {
    for (;;) {
      auto *trans = source_.resp_port->read();
      if (trans->is_read()) {
        read_response_order_.Complete(trans);
        read_completion_event_.notify(sc_core::SC_ZERO_TIME);
      } else {
        write_response_order_.Complete(trans);
        write_completion_event_.notify(sc_core::SC_ZERO_TIME);
      }
    }
  }

  void ReadResponseThread() {
    for (;;) {
      auto *trans = read_response_order_.TakeReady();
      if (trans == nullptr) {
        wait(read_completion_event_);
        continue;
      }
      auto it = pending_reads_.find(trans);
      if (it == pending_reads_.end()) {
        continue;
      }
      auto &pending = it->second;
      const auto generation = pending.reset_generation;
      if (!rst_ni_.read() || pending.reset_generation != reset_generation_) {
        r_valid.write(false);
        r_last.write(false);
        CleanupTransaction(trans);
        pending_reads_.erase(trans);
        ReleaseTransactionSlot(generation);
        continue;
      }

      const auto resp = AxiRespFromTlmStatus(trans->get_response_status());
      const auto beat_bytes = pending.ext.BytesPerBeat();
      const auto total_beats = pending.ext.NumBeats();
      auto *data = trans->get_data_ptr();

      // Recreate AXI R beats from the completed TLM read buffer.
      bool aborted = false;
      for (size_t beat = 0; beat < total_beats; ++beat) {
        if (!rst_ni_.read() || pending.reset_generation != reset_generation_) {
          aborted = true;
          break;
        }
        const auto beat_addr = BeatAddress(trans->get_address(), beat,
                                           beat_bytes, pending.ext.burst_type);
        const auto packed = PackWriteData(beat_addr, data + beat * beat_bytes,
                                          beat_bytes, kBusBytes);
        LV_TRACE("{} R beat={} addr={:#x} data={:#x} last={}", name(), beat,
                 beat_addr, packed, beat + 1 == total_beats);
        r_id.write(pending.ext.txn_id);
        r_data.write(packed);
        r_resp.write(static_cast<uint32_t>(resp));
        r_last.write(beat + 1 == total_beats);
        r_valid.write(true);
        do {
          wait(clk_i_.posedge_event());
        } while (rst_ni_.read() && !r_ready.read());
        if (!rst_ni_.read() || pending.reset_generation != reset_generation_) {
          aborted = true;
          break;
        }
        if (read_beat_gap_cycles_ > 0 && beat + 1 < total_beats) {
          r_valid.write(false);
          r_last.write(false);
          WaitCycles(read_beat_gap_cycles_);
        }
      }
      r_valid.write(false);
      r_last.write(false);
      WaitCycles(read_burst_gap_cycles_);

      if (aborted || !rst_ni_.read() ||
          pending.reset_generation != reset_generation_) {
        r_valid.write(false);
        r_last.write(false);
        CleanupTransaction(trans);
        pending_reads_.erase(trans);
        ReleaseTransactionSlot(generation);
        continue;
      }

      CleanupTransaction(trans);
      pending_reads_.erase(trans);
      ReleaseTransactionSlot(generation);
    }
  }

  void WriteResponseThread() {
    for (;;) {
      auto *trans = write_response_order_.TakeReady();
      if (trans == nullptr) {
        wait(write_completion_event_);
        continue;
      }
      auto it = pending_writes_.find(trans);
      if (it == pending_writes_.end()) {
        continue;
      }
      auto &pending = it->second;
      const auto generation = pending.reset_generation;
      if (!rst_ni_.read() || pending.reset_generation != reset_generation_) {
        b_valid.write(false);
        CleanupTransaction(trans);
        pending_writes_.erase(trans);
        ReleaseTransactionSlot(generation);
        continue;
      }

      const auto resp = AxiRespFromTlmStatus(trans->get_response_status());
      WaitCycles(write_response_gap_cycles_);
      if (!rst_ni_.read() || pending.reset_generation != reset_generation_) {
        b_valid.write(false);
        CleanupTransaction(trans);
        pending_writes_.erase(trans);
        ReleaseTransactionSlot(generation);
        continue;
      }
      b_id.write(pending.ext.txn_id);
      b_resp.write(static_cast<uint32_t>(resp));
      b_valid.write(true);
      // BRESP stays valid until the AXI master accepts it.
      do {
        wait(clk_i_.posedge_event());
      } while (rst_ni_.read() && !b_ready.read());
      b_valid.write(false);

      if (!rst_ni_.read() || pending.reset_generation != reset_generation_) {
        b_valid.write(false);
        CleanupTransaction(trans);
        pending_writes_.erase(trans);
        ReleaseTransactionSlot(generation);
        continue;
      }

      CleanupTransaction(trans);
      pending_writes_.erase(trans);
      ReleaseTransactionSlot(generation);
    }
  }

  void WaitForReady() {
    while (!rst_ni_.read() || target_ == nullptr) {
      wait(clk_i_.posedge_event());
    }
  }

  bool WaitForWriteAddress(PendingWriteAddress &address) {
    for (;;) {
      if (!rst_ni_.read()) {
        ResetWriteQueues();
        return false;
      }
      if (write_addr_fifo_.nb_read(address)) {
        return true;
      }
      wait(clk_i_.posedge_event());
    }
  }

  bool WaitForWriteBeat(WriteBeat &beat) {
    for (;;) {
      if (!rst_ni_.read()) {
        ResetWriteQueues();
        return false;
      }
      if (write_data_fifo_.nb_read(beat)) {
        return true;
      }
      wait(clk_i_.posedge_event());
    }
  }

  void ResetWriteQueues() {
    PendingWriteAddress address;
    while (write_addr_fifo_.nb_read(address)) {
    }
    WriteBeat beat;
    while (write_data_fifo_.nb_read(beat)) {
    }
  }

  void ResetMonitorThread() {
    for (;;) {
      wait(clk_i_.posedge_event());
      if (!rst_ni_.read()) {
        if (!reset_active_) {
          ++reset_generation_;
          reset_active_ = true;
          if (config_.serialize_transactions) {
            transaction_active_ = false;
            transaction_released_.notify(sc_core::SC_ZERO_TIME);
          }
        }
      } else {
        reset_active_ = false;
      }
    }
  }

  static uint32_t EnvUInt(const char *name, uint32_t default_value) {
    const char *value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
      return default_value;
    }
    char *end = nullptr;
    const auto parsed = std::strtoul(value, &end, 0);
    return end == value ? default_value : static_cast<uint32_t>(parsed);
  }

  void WaitCycles(uint32_t cycles) {
    for (uint32_t cycle = 0; cycle < cycles && rst_ni_.read(); ++cycle) {
      wait(clk_i_.posedge_event());
    }
  }

  // This adapter owns incoming AXI geometry validation; do not rely on a
  // permissive downstream RAM to diagnose malformed pin-level requests.
  size_t ValidateGeometry(const char *operation, uint64_t address,
                          uint32_t length, uint32_t size,
                          uint32_t burst) const {
    if (size > BytesToSizeCode(kBusBytes)) {
      LV_FATAL("{} AXI->TLM {}: AxSIZE exceeds data bus width (size={})",
               name(), operation, size);
    }
    if (length > 255) {
      LV_FATAL("{} AXI->TLM {}: AxLEN exceeds 255 (len={})", name(), operation,
               length);
    }
    if (burst != kIncrBurst) {
      LV_FATAL(
          "{} AXI->TLM {}: {} AXI bursts are unsupported "
          "(addr={:#x}, burst={})",
          name(), operation,
          burst == kFixedBurst  ? "FIXED"
          : burst == kWrapBurst ? "WRAP"
                                : "reserved",
          address, burst);
    }
    const size_t beat_bytes = SizeCodeToBytes(size);
    if (address % beat_bytes != 0) {
      LV_FATAL(
          "{} AXI->TLM {}: unaligned AXI beats are unsupported "
          "(addr={:#x}, size={})",
          name(), operation, address, size);
    }
    const size_t total_bytes = (static_cast<size_t>(length) + 1) * beat_bytes;
    constexpr uint64_t max_address = (uint64_t{1} << AddrW) - 1;
    if (address > max_address || total_bytes - 1 > max_address - address) {
      LV_FATAL(
          "{} AXI->TLM {}: burst exceeds address range (addr={:#x}, bytes={})",
          name(), operation, address, total_bytes);
    }
    if (total_bytes > 4096 - address % 4096) {
      LV_FATAL(
          "{} AXI->TLM {}: burst crosses 4-KiB boundary (addr={:#x}, bytes={})",
          name(), operation, address, total_bytes);
    }
    return total_bytes;
  }

  bool AcquireTransactionSlot(uint64_t &generation) {
    while (config_.serialize_transactions && transaction_active_) {
      wait(transaction_released_);
      if (!rst_ni_.read()) return false;
    }
    if (!rst_ni_.read()) return false;
    generation = reset_generation_;
    if (config_.serialize_transactions) transaction_active_ = true;
    return true;
  }

  void ReleaseTransactionSlot(uint64_t generation) {
    // Reset frees the slot; old completions cannot release its new owner.
    if (!config_.serialize_transactions || !transaction_active_ ||
        generation != reset_generation_) {
      return;
    }
    transaction_active_ = false;
    transaction_released_.notify(sc_core::SC_ZERO_TIME);
  }

  void CleanupTransaction(tlm::tlm_generic_payload *trans) {
    auto *ext = static_cast<AxiExtension *>(nullptr);
    trans->get_extension(ext);
    if (ext != nullptr) {
      trans->clear_extension(ext);
      delete ext;
    }
    // The pending map owns data buffers; Pool owns the payload object itself.
    trans->release();
  }
};

}  // namespace lv::axi
