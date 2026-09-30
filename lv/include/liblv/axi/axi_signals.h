/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <systemc.h>

#include <cstddef>
#include <cstdint>

namespace lv::axi {

/**
 * SystemC signal bundle for a minimal AXI4 pin-level interface.
 *
 * The bundle covers AR/R/AW/W/B channels used by the adapters. Optional
 * sideband signals such as AxCACHE/AxPROT are carried in AxiExtension metadata
 * but are not represented as pins in this v1 bundle.
 */
template <size_t AddrW, size_t DataW, size_t IdW>
struct AxiSignals {
  static_assert(AddrW <= 32,
                "This adapter currently supports up to 32-bit addresses");
  static_assert(DataW <= 64,
                "This adapter currently supports up to 64-bit data");
  static_assert(IdW <= 32, "This adapter currently supports up to 32-bit IDs");

  // Read address channel.
  sc_core::sc_signal<bool> *ar_valid = nullptr;
  sc_core::sc_signal<bool> *ar_ready = nullptr;
  sc_core::sc_signal<uint32_t> *ar_id = nullptr;
  sc_core::sc_signal<uint32_t> *ar_addr = nullptr;
  sc_core::sc_signal<uint32_t> *ar_len = nullptr;
  sc_core::sc_signal<uint32_t> *ar_size = nullptr;
  sc_core::sc_signal<uint32_t> *ar_burst = nullptr;

  // Read data channel.
  sc_core::sc_signal<bool> *r_valid = nullptr;
  sc_core::sc_signal<bool> *r_ready = nullptr;
  sc_core::sc_signal<uint32_t> *r_id = nullptr;
  sc_core::sc_signal<uint64_t> *r_data = nullptr;
  sc_core::sc_signal<uint32_t> *r_resp = nullptr;
  sc_core::sc_signal<bool> *r_last = nullptr;

  // Write address channel.
  sc_core::sc_signal<bool> *aw_valid = nullptr;
  sc_core::sc_signal<bool> *aw_ready = nullptr;
  sc_core::sc_signal<uint32_t> *aw_id = nullptr;
  sc_core::sc_signal<uint32_t> *aw_addr = nullptr;
  sc_core::sc_signal<uint32_t> *aw_len = nullptr;
  sc_core::sc_signal<uint32_t> *aw_size = nullptr;
  sc_core::sc_signal<uint32_t> *aw_burst = nullptr;

  // Write data channel.
  sc_core::sc_signal<bool> *w_valid = nullptr;
  sc_core::sc_signal<bool> *w_ready = nullptr;
  sc_core::sc_signal<uint64_t> *w_data = nullptr;
  sc_core::sc_signal<uint32_t> *w_strb = nullptr;
  sc_core::sc_signal<bool> *w_last = nullptr;

  // Write response channel. b_id may be null when the RTL has no BID pin.
  sc_core::sc_signal<bool> *b_valid = nullptr;
  sc_core::sc_signal<bool> *b_ready = nullptr;
  sc_core::sc_signal<uint32_t> *b_id = nullptr;
  sc_core::sc_signal<uint32_t> *b_resp = nullptr;
};

/**
 * Owning SystemC signal storage for a minimal AXI4 pin-level interface.
 *
 * AxiSignals is a non-owning view, which keeps adapter binding flexible. This
 * bundle owns the actual sc_signal objects and exposes them through pins().
 */
template <size_t AddrW, size_t DataW, size_t IdW>
struct AxiSignalBundle {
  static_assert(AddrW <= 32,
                "This adapter currently supports up to 32-bit addresses");
  static_assert(DataW <= 64,
                "This adapter currently supports up to 64-bit data");
  static_assert(IdW <= 32, "This adapter currently supports up to 32-bit IDs");

  [[nodiscard]] AxiSignals<AddrW, DataW, IdW> pins() {
    AxiSignals<AddrW, DataW, IdW> signals;
    signals.ar_valid = &ar_valid;
    signals.ar_ready = &ar_ready;
    signals.ar_id = &ar_id;
    signals.ar_addr = &ar_addr;
    signals.ar_len = &ar_len;
    signals.ar_size = &ar_size;
    signals.ar_burst = &ar_burst;
    signals.r_valid = &r_valid;
    signals.r_ready = &r_ready;
    signals.r_id = &r_id;
    signals.r_data = &r_data;
    signals.r_resp = &r_resp;
    signals.r_last = &r_last;
    signals.aw_valid = &aw_valid;
    signals.aw_ready = &aw_ready;
    signals.aw_id = &aw_id;
    signals.aw_addr = &aw_addr;
    signals.aw_len = &aw_len;
    signals.aw_size = &aw_size;
    signals.aw_burst = &aw_burst;
    signals.w_valid = &w_valid;
    signals.w_ready = &w_ready;
    signals.w_data = &w_data;
    signals.w_strb = &w_strb;
    signals.w_last = &w_last;
    signals.b_valid = &b_valid;
    signals.b_ready = &b_ready;
    signals.b_id = &b_id;
    signals.b_resp = &b_resp;
    return signals;
  }

  // Read address channel.
  sc_core::sc_signal<bool> ar_valid;
  sc_core::sc_signal<bool> ar_ready;
  sc_core::sc_signal<uint32_t> ar_id;
  sc_core::sc_signal<uint32_t> ar_addr;
  sc_core::sc_signal<uint32_t> ar_len;
  sc_core::sc_signal<uint32_t> ar_size;
  sc_core::sc_signal<uint32_t> ar_burst;

  // Read data channel.
  sc_core::sc_signal<bool> r_valid;
  sc_core::sc_signal<bool> r_ready;
  sc_core::sc_signal<uint32_t> r_id;
  sc_core::sc_signal<uint64_t> r_data;
  sc_core::sc_signal<uint32_t> r_resp;
  sc_core::sc_signal<bool> r_last;

  // Write address channel.
  sc_core::sc_signal<bool> aw_valid;
  sc_core::sc_signal<bool> aw_ready;
  sc_core::sc_signal<uint32_t> aw_id;
  sc_core::sc_signal<uint32_t> aw_addr;
  sc_core::sc_signal<uint32_t> aw_len;
  sc_core::sc_signal<uint32_t> aw_size;
  sc_core::sc_signal<uint32_t> aw_burst;

  // Write data channel.
  sc_core::sc_signal<bool> w_valid;
  sc_core::sc_signal<bool> w_ready;
  sc_core::sc_signal<uint64_t> w_data;
  sc_core::sc_signal<uint32_t> w_strb;
  sc_core::sc_signal<bool> w_last;

  // Write response channel.
  sc_core::sc_signal<bool> b_valid;
  sc_core::sc_signal<bool> b_ready;
  sc_core::sc_signal<uint32_t> b_id;
  sc_core::sc_signal<uint32_t> b_resp;
};

}  // namespace lv::axi
