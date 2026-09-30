/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <tlm.h>

#include <cstddef>
#include <cstdint>

namespace lv::axi {

/** AXI burst type encoding used by ARBURST/AWBURST. */
enum AxiBurstType : uint8_t {
  kFixedBurst =
      0,  ///< Fixed address for every beat. Adapters terminate with LV_FATAL.
  kIncrBurst = 1,  ///< Address increments by one beat size.
  kWrapBurst =
      2,  ///< Wrapped incrementing burst. Not supported by v1 adapters.
};

/** AXI response encoding used by RRESP/BRESP. */
enum AxiResp : uint8_t {
  kOkayResp = 0,    ///< Normal access success.
  kExOkayResp = 1,  ///< Exclusive access success.
  kSlvErrResp = 2,  ///< Slave-side failure.
  kDecErrResp = 3,  ///< Decode/address failure.
};

/** Configuration shared by AXI-to-TLM and TLM-to-AXI adapters. */
struct AxiAdapterConfig {
  /// Response value used when a read request has no explicit AXI response yet.
  AxiResp default_read_resp = kOkayResp;
  /// Response value used when a write request has no explicit AXI response yet.
  AxiResp default_write_resp = kOkayResp;
  /// Split a TLM burst into single-beat AXI transactions when true.
  bool allow_burst_split = false;
  /// Accept only one AXI transaction at a time across read and write channels.
  bool serialize_transactions = false;
  /// Reject TLM transactions that do not carry an AxiExtension.
  bool require_extension = false;
};

/**
 * TLM generic-payload extension carrying AXI metadata.
 *
 * TLM payloads already carry command/address/data, but AXI needs extra fields
 * such as ID, burst length, beat size, burst type, and response. Attach this
 * extension to preserve that information across the adapter boundary.
 */
class AxiExtension : public tlm::tlm_extension<AxiExtension> {
 public:
  /// AXI AxID value.
  uint32_t txn_id = 0;
  /// AXI AxLEN encoding: number of beats minus one.
  uint32_t burst_len = 0;
  /// AXI AxSIZE encoding: log2(bytes per beat).
  uint8_t beat_size = 0;
  /// AXI AxBURST value.
  uint8_t burst_type = kIncrBurst;
  /// AXI AxLOCK value, carried as metadata only in v1 adapters.
  uint8_t lock = 0;
  /// AXI AxCACHE value, carried as metadata only in v1 adapters.
  uint8_t cache = 0;
  /// AXI AxPROT value, carried as metadata only in v1 adapters.
  uint8_t prot = 0;
  /// AXI AxQOS value, carried as metadata only in v1 adapters.
  uint8_t qos = 0;
  /// Aggregated AXI response observed for the transaction.
  AxiResp axi_resp = kOkayResp;

  tlm::tlm_extension_base *clone() const override {
    return new AxiExtension(*this);
  }

  void copy_from(const tlm::tlm_extension_base &ext) override {
    const auto &other = static_cast<const AxiExtension &>(ext);
    *this = other;
  }

  /** Returns the number of AXI beats represented by this extension. */
  [[nodiscard]] size_t NumBeats() const {
    return static_cast<size_t>(burst_len) + 1;
  }

  /** Returns the number of bytes in each AXI beat. */
  [[nodiscard]] size_t BytesPerBeat() const { return size_t{1} << beat_size; }
};

}  // namespace lv::axi
