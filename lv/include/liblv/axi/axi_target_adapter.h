/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/axi/axi_helpers.h>
#include <liblv/axi/axi_signals.h>
#include <liblv/common/tlm_sink.h>
#include <liblv/common/tlm_source.h>
#include <liblv/log.h>
#include <systemc.h>
#include <tlm.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <stdexcept>

namespace lv::axi {

/**
 * TLM target socket to AXI pin-level master adapter.
 *
 * Use this adapter when a TLM initiator needs to access an RTL AXI slave. The
 * incoming TLM generic payload is converted into AXI AR/R or AW/W/B handshakes
 * on the public SystemC ports. Only aligned, complete beats are accepted; FIXED
 * bursts terminate the simulation with LV_FATAL.
 */
template <size_t AddrW, size_t DataW, size_t IdW>
class AxiTargetAdapter : public sc_core::sc_module {
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
  explicit AxiTargetAdapter(const sc_core::sc_module_name &name,
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
        sink_("sink"),
        config_(config) {
    SC_THREAD(RequestThread);
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
      has_b_id_ = false;
    }
    b_resp.bind(*pins.b_resp);
  }

  /** Returns the TLM target socket that accepts incoming transactions. */
  [[nodiscard]] Target *target_socket() { return &sink_.port; }

  // Clock and active-low reset.
  sc_core::sc_in<bool> clk_i_;
  sc_core::sc_in<bool> rst_ni_;

  // Read address channel driven by this adapter.
  sc_core::sc_out<bool> ar_valid;
  sc_core::sc_in<bool> ar_ready;
  sc_core::sc_out<uint32_t> ar_id;
  sc_core::sc_out<uint32_t> ar_addr;
  sc_core::sc_out<uint32_t> ar_len;
  sc_core::sc_out<uint32_t> ar_size;
  sc_core::sc_out<uint32_t> ar_burst;

  // Read data channel consumed by this adapter.
  sc_core::sc_in<bool> r_valid;
  sc_core::sc_out<bool> r_ready;
  sc_core::sc_in<uint32_t> r_id;
  sc_core::sc_in<uint64_t> r_data;
  sc_core::sc_in<uint32_t> r_resp;
  sc_core::sc_in<bool> r_last;

  // Write address channel driven by this adapter.
  sc_core::sc_out<bool> aw_valid;
  sc_core::sc_in<bool> aw_ready;
  sc_core::sc_out<uint32_t> aw_id;
  sc_core::sc_out<uint32_t> aw_addr;
  sc_core::sc_out<uint32_t> aw_len;
  sc_core::sc_out<uint32_t> aw_size;
  sc_core::sc_out<uint32_t> aw_burst;

  // Write data channel driven by this adapter.
  sc_core::sc_out<bool> w_valid;
  sc_core::sc_in<bool> w_ready;
  sc_core::sc_out<uint64_t> w_data;
  sc_core::sc_out<uint32_t> w_strb;
  sc_core::sc_out<bool> w_last;

  // Write response channel consumed by this adapter.
  sc_core::sc_in<bool> b_valid;
  sc_core::sc_out<bool> b_ready;
  sc_core::sc_in<uint32_t> b_id;
  sc_core::sc_in<uint32_t> b_resp;

 private:
  static constexpr size_t kBusBytes = DataW / 8;

  struct ResolvedTxn {
    /// Resolved AXI metadata used to drive the pin-level transaction.
    AxiExtension ext;
    /// Original payload extension, if one was attached by the caller.
    AxiExtension *original_ext = nullptr;
  };

  sc_core::sc_signal<uint32_t> dummy_b_id_{"dummy_b_id"};
  bool has_b_id_ = true;
  lv::TlmSink sink_;
  AxiAdapterConfig config_;

  void RequestThread() {
    for (;;) {
      auto *trans = sink_.req_port->read();
      while (!rst_ni_.read()) {
        ResetOutputs();
        wait(clk_i_.posedge_event());
      }
      HandleTransaction(trans);
      // The TLM response is not returned until the AXI handshake completes.
      sink_.resp_port->write(trans);
    }
  }

  void ResetOutputs() {
    ar_valid.write(false);
    ar_id.write(0);
    ar_addr.write(0);
    ar_len.write(0);
    ar_size.write(0);
    ar_burst.write(kIncrBurst);
    r_ready.write(false);
    aw_valid.write(false);
    aw_id.write(0);
    aw_addr.write(0);
    aw_len.write(0);
    aw_size.write(0);
    aw_burst.write(kIncrBurst);
    w_valid.write(false);
    w_data.write(0);
    w_strb.write(0);
    w_last.write(false);
    b_ready.write(false);
  }

  ResolvedTxn ResolveTransaction(tlm::tlm_generic_payload *trans) const {
    ResolvedTxn resolved;
    trans->get_extension(resolved.original_ext);
    if (resolved.original_ext != nullptr) {
      resolved.ext = *resolved.original_ext;
      return resolved;
    }

    if (config_.require_extension) {
      throw std::runtime_error("AXI extension required");
    }

    // Synthesize basic INCR burst metadata for plain TLM payloads.
    const size_t data_length = std::max<size_t>(1, trans->get_data_length());
    const size_t beat_bytes = std::min(kBusBytes, data_length);
    resolved.ext.txn_id = 0;
    resolved.ext.beat_size = static_cast<uint8_t>(BytesToSizeCode(beat_bytes));
    resolved.ext.burst_type = kIncrBurst;
    resolved.ext.burst_len =
        static_cast<uint32_t>((data_length + beat_bytes - 1) / beat_bytes) - 1;
    resolved.ext.axi_resp = trans->is_read() ? config_.default_read_resp
                                             : config_.default_write_resp;
    return resolved;
  }

  void FailTransaction(tlm::tlm_generic_payload *trans, AxiExtension *ext,
                       AxiResp axi_resp,
                       tlm::tlm_response_status status) const {
    if (ext != nullptr) {
      ext->axi_resp = axi_resp;
    }
    trans->set_response_status(status);
  }

  // Validate the entire payload before issuing even the first AXI beat.
  tlm::tlm_response_status ValidateTransaction(
      const tlm::tlm_generic_payload &trans, const AxiExtension &ext) const {
    if (trans.get_data_ptr() == nullptr) {
      return tlm::TLM_GENERIC_ERROR_RESPONSE;
    }
    if (!IsSupportedBurst(ext.burst_type) ||
        ext.beat_size >= std::numeric_limits<size_t>::digits ||
        ext.BytesPerBeat() > kBusBytes) {
      return tlm::TLM_BURST_ERROR_RESPONSE;
    }
    const size_t beat_bytes = ext.BytesPerBeat();
    const size_t data_length = trans.get_data_length();
    // Division avoids overflowing a metadata-derived beat-count product.
    if (data_length == 0 || trans.get_streaming_width() < data_length ||
        data_length % beat_bytes != 0 ||
        ext.NumBeats() != data_length / beat_bytes) {
      return tlm::TLM_BURST_ERROR_RESPONSE;
    }
    constexpr uint64_t max_address = (uint64_t{1} << AddrW) - 1;
    const auto address = trans.get_address();
    if (address % beat_bytes != 0 || address > max_address ||
        data_length - 1 > max_address - address) {
      return tlm::TLM_BURST_ERROR_RESPONSE;
    }
    // Split mode emits single-beat transactions, each with its own address.
    if (!config_.allow_burst_split &&
        (ext.NumBeats() > 256 || data_length > 4096 - address % 4096)) {
      return tlm::TLM_BURST_ERROR_RESPONSE;
    }
    for (size_t offset = 0; offset < data_length; offset += beat_bytes) {
      if (!FitsWithinBusBeat(address + offset, beat_bytes, kBusBytes)) {
        return tlm::TLM_BURST_ERROR_RESPONSE;
      }
    }
    if (trans.get_byte_enable_ptr() != nullptr &&
        trans.get_byte_enable_length() == 0) {
      return tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE;
    }
    return tlm::TLM_OK_RESPONSE;
  }

  void HandleTransaction(tlm::tlm_generic_payload *trans) {
    auto *original_ext = trans->get_extension<AxiExtension>();
    try {
      auto resolved = ResolveTransaction(trans);
      if (resolved.ext.burst_type == kFixedBurst) {
        LV_FATAL(
            "{} TLM->AXI {}: FIXED AXI bursts are unsupported "
            "(addr={:#x}, id={}, burst={})",
            name(), trans->is_read() ? "read" : "write", trans->get_address(),
            resolved.ext.txn_id, resolved.ext.burst_type);
      }
      const auto status = ValidateTransaction(*trans, resolved.ext);
      if (status != tlm::TLM_OK_RESPONSE) {
        FailTransaction(trans, resolved.original_ext, kSlvErrResp, status);
        return;
      }

      if (trans->is_read()) {
        if (config_.allow_burst_split && resolved.ext.burst_len > 0) {
          HandleSplitRead(trans, resolved);
        } else {
          const auto resp = HandleRead(trans, resolved, trans->get_address(),
                                       resolved.ext.burst_len, 0);
          if (resolved.original_ext != nullptr) {
            resolved.original_ext->axi_resp = resp;
          }
          trans->set_response_status(TlmStatusFromAxiResp(resp));
        }
      } else if (trans->is_write()) {
        if (config_.allow_burst_split && resolved.ext.burst_len > 0) {
          HandleSplitWrite(trans, resolved);
        } else {
          const auto resp = HandleWrite(trans, resolved, trans->get_address(),
                                        resolved.ext.burst_len, 0);
          if (resolved.original_ext != nullptr) {
            resolved.original_ext->axi_resp = resp;
          }
          trans->set_response_status(TlmStatusFromAxiResp(resp));
        }
      } else {
        FailTransaction(trans, resolved.original_ext, kSlvErrResp,
                        tlm::TLM_COMMAND_ERROR_RESPONSE);
      }
    } catch (const lv::fatal_error &) {
      throw;
    } catch (const std::invalid_argument &) {
      FailTransaction(trans, original_ext, kSlvErrResp,
                      tlm::TLM_BURST_ERROR_RESPONSE);
    } catch (const std::exception &) {
      FailTransaction(trans, original_ext, kSlvErrResp,
                      tlm::TLM_GENERIC_ERROR_RESPONSE);
    }
  }

  void HandleSplitRead(tlm::tlm_generic_payload *trans,
                       const ResolvedTxn &resolved) {
    const auto beat_bytes = resolved.ext.BytesPerBeat();
    const auto total_beats = resolved.ext.NumBeats();
    AxiResp aggregate_resp = kOkayResp;

    // Some RTL blocks only accept single-beat accesses. Splitting keeps the
    // caller-facing TLM payload intact while issuing one AXI beat at a time.
    for (size_t beat = 0; beat < total_beats; ++beat) {
      const auto beat_addr = BeatAddress(trans->get_address(), beat, beat_bytes,
                                         resolved.ext.burst_type);
      auto beat_ext = resolved.ext;
      beat_ext.burst_len = 0;
      aggregate_resp = AggregateResp(
          aggregate_resp,
          HandleRead(trans, ResolvedTxn{beat_ext, resolved.original_ext},
                     beat_addr, 0, beat));
      if (aggregate_resp != kOkayResp) {
        break;
      }
    }

    if (resolved.original_ext != nullptr) {
      resolved.original_ext->axi_resp = aggregate_resp;
    }
    trans->set_response_status(TlmStatusFromAxiResp(aggregate_resp));
  }

  void HandleSplitWrite(tlm::tlm_generic_payload *trans,
                        const ResolvedTxn &resolved) {
    const auto beat_bytes = resolved.ext.BytesPerBeat();
    const auto total_beats = resolved.ext.NumBeats();
    AxiResp aggregate_resp = kOkayResp;

    // Preserve the original TLM byte buffer and offset into each split beat.
    for (size_t beat = 0; beat < total_beats; ++beat) {
      const auto beat_addr = BeatAddress(trans->get_address(), beat, beat_bytes,
                                         resolved.ext.burst_type);
      auto beat_ext = resolved.ext;
      beat_ext.burst_len = 0;
      aggregate_resp = AggregateResp(
          aggregate_resp,
          HandleWrite(trans, ResolvedTxn{beat_ext, resolved.original_ext},
                      beat_addr, 0, beat));
      if (aggregate_resp != kOkayResp) {
        break;
      }
    }

    if (resolved.original_ext != nullptr) {
      resolved.original_ext->axi_resp = aggregate_resp;
    }
    trans->set_response_status(TlmStatusFromAxiResp(aggregate_resp));
  }

  AxiResp HandleRead(tlm::tlm_generic_payload *trans,
                     const ResolvedTxn &resolved, uint64_t base_addr,
                     uint32_t burst_len, size_t split_offset_beats) {
    const auto beat_bytes = resolved.ext.BytesPerBeat();
    const auto total_beats = static_cast<size_t>(burst_len) + 1;
    auto *data = trans->get_data_ptr();

    if (!DriveReadAddress(base_addr, resolved.ext, burst_len)) {
      return kSlvErrResp;
    }

    AxiResp aggregate_resp = kOkayResp;
    r_ready.write(true);
    for (size_t beat = 0; beat < total_beats; ++beat) {
      if (!rst_ni_.read()) {
        ResetOutputs();
        return kSlvErrResp;
      }
      do {
        wait(clk_i_.posedge_event());
        if (!rst_ni_.read()) {
          ResetOutputs();
          return kSlvErrResp;
        }
      } while (!r_valid.read());
      if (r_id.read() != resolved.ext.txn_id) {
        LV_FATAL(
            "{} TLM->AXI read: RID does not match ARID (expected={}, got={})",
            name(), resolved.ext.txn_id, r_id.read());
      }
      if (r_last.read() != (beat + 1 == total_beats)) {
        LV_FATAL(
            "{} TLM->AXI read: RLAST does not match ARLEN "
            "(beat={}, beats={}, last={})",
            name(), beat, total_beats, r_last.read());
      }
      const auto beat_addr =
          BeatAddress(base_addr, beat, beat_bytes, resolved.ext.burst_type);
      const auto byte_offset = (split_offset_beats + beat) * beat_bytes;
      std::array<uint8_t, kBusBytes> beat_data;
      UnpackReadData(beat_addr, r_data.read(), beat_data.data(), beat_bytes,
                     kBusBytes);
      const auto *byte_enable = trans->get_byte_enable_ptr();
      for (size_t i = 0; i < beat_bytes; ++i) {
        if (byte_enable == nullptr ||
            byte_enable[(byte_offset + i) % trans->get_byte_enable_length()] ==
                TLM_BYTE_ENABLED) {
          data[byte_offset + i] = beat_data[i];
        }
      }
      aggregate_resp =
          AggregateResp(aggregate_resp, static_cast<AxiResp>(r_resp.read()));
    }
    r_ready.write(false);
    return aggregate_resp;
  }

  AxiResp HandleWrite(tlm::tlm_generic_payload *trans,
                      const ResolvedTxn &resolved, uint64_t base_addr,
                      uint32_t burst_len, size_t split_offset_beats) {
    const auto total_beats = static_cast<size_t>(burst_len) + 1;

    if (!DriveWriteChannels(trans, resolved, base_addr, total_beats,
                            split_offset_beats)) {
      return kSlvErrResp;
    }

    b_ready.write(true);
    do {
      wait(clk_i_.posedge_event());
      if (!rst_ni_.read()) {
        ResetOutputs();
        return kSlvErrResp;
      }
    } while (!b_valid.read());
    if (has_b_id_ && b_id.read() != resolved.ext.txn_id) {
      LV_FATAL(
          "{} TLM->AXI write: BID does not match AWID (expected={}, got={})",
          name(), resolved.ext.txn_id, b_id.read());
    }
    const auto resp = static_cast<AxiResp>(b_resp.read());
    b_ready.write(false);
    return resp;
  }

  bool DriveReadAddress(uint64_t addr, const AxiExtension &ext,
                        uint32_t burst_len) {
    ar_id.write(ext.txn_id);
    ar_addr.write(static_cast<uint32_t>(addr));
    ar_len.write(burst_len);
    ar_size.write(ext.beat_size);
    ar_burst.write(ext.burst_type);
    ar_valid.write(true);
    // AXI valid remains asserted until the slave accepts the address.
    do {
      wait(clk_i_.posedge_event());
      if (!rst_ni_.read()) {
        ResetOutputs();
        return false;
      }
    } while (!ar_ready.read());
    ar_valid.write(false);
    return true;
  }

  bool DriveWriteChannels(tlm::tlm_generic_payload *trans,
                          const ResolvedTxn &resolved, uint64_t base_addr,
                          size_t total_beats, size_t split_offset_beats) {
    const auto beat_bytes = resolved.ext.BytesPerBeat();
    auto *data = trans->get_data_ptr();

    aw_id.write(resolved.ext.txn_id);
    aw_addr.write(static_cast<uint32_t>(base_addr));
    aw_len.write(static_cast<uint32_t>(total_beats - 1));
    aw_size.write(resolved.ext.beat_size);
    aw_burst.write(resolved.ext.burst_type);
    aw_valid.write(true);

    auto drive_beat = [&](size_t beat) {
      const auto beat_addr =
          BeatAddress(base_addr, beat, beat_bytes, resolved.ext.burst_type);
      const auto byte_offset = (split_offset_beats + beat) * beat_bytes;
      const auto *beat_data = data + byte_offset;
      auto packed = PackWriteData(beat_addr, beat_data, beat_bytes, kBusBytes);
      auto strobe = BuildWriteStrobe(beat_addr, beat_bytes, kBusBytes);
      auto *byte_enable = trans->get_byte_enable_ptr();
      if (byte_enable != nullptr) {
        strobe = 0;
        const auto lane = static_cast<size_t>(beat_addr % kBusBytes);
        for (size_t i = 0; i < beat_bytes; ++i) {
          if (byte_enable[(byte_offset + i) %
                          trans->get_byte_enable_length()] ==
              TLM_BYTE_ENABLED) {
            strobe |= (1u << (lane + i));
          }
        }
      }
      w_data.write(packed);
      w_strb.write(strobe);
      w_last.write(beat + 1 == total_beats);
      w_valid.write(true);
    };

    size_t beat = 0;
    bool address_done = false;
    drive_beat(beat);
    while (!address_done || beat < total_beats) {
      wait(clk_i_.posedge_event());
      if (!rst_ni_.read()) {
        ResetOutputs();
        return false;
      }
      if (!address_done && aw_ready.read()) {
        address_done = true;
        aw_valid.write(false);
      }
      if (beat < total_beats && w_valid.read() && w_ready.read()) {
        ++beat;
        if (beat == total_beats) {
          w_valid.write(false);
          w_last.write(false);
        } else {
          drive_beat(beat);
        }
      }
    }

    // WVALID/WLAST are already deasserted; wait for the independent B channel.
    w_data.write(0);
    w_strb.write(0);
    return true;
  }
};

}  // namespace lv::axi
