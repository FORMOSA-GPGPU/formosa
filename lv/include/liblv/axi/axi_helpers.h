/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/axi/axi_types.h>
#include <tlm.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace lv::axi {

/** Converts an AXI AxSIZE encoding into bytes per beat. */
inline size_t SizeCodeToBytes(uint32_t size_code) {
  return size_t{1} << size_code;
}

/** Converts a power-of-two byte count into an AXI AxSIZE encoding. */
uint32_t BytesToSizeCode(size_t num_bytes);

/** Maps a TLM response status to the closest AXI RRESP/BRESP value. */
AxiResp AxiRespFromTlmStatus(tlm::tlm_response_status status);

/** Maps an AXI RRESP/BRESP value back to a TLM response status. */
tlm::tlm_response_status TlmStatusFromAxiResp(AxiResp resp);

/** Returns true for burst modes implemented by the current adapters. */
inline bool IsSupportedBurst(uint8_t burst_type) {
  return burst_type == kIncrBurst;
}

/** Computes the byte address for one beat of a FIXED or INCR burst. */
inline uint64_t BeatAddress(uint64_t base_addr, size_t beat_index,
                            size_t beat_bytes, uint8_t burst_type) {
  if (burst_type == kFixedBurst) {
    return base_addr;
  }
  return base_addr + beat_index * beat_bytes;
}

/** Returns true when an access fits within a single data-bus word. */
inline bool FitsWithinBusBeat(uint64_t addr, size_t beat_bytes,
                              size_t bus_bytes) {
  const auto lane = static_cast<size_t>(addr % bus_bytes);
  return lane + beat_bytes <= bus_bytes;
}

/** Builds WSTRB bits for a beat at the given byte address. */
uint32_t BuildWriteStrobe(uint64_t addr, size_t beat_bytes, size_t bus_bytes);

/**
 * Packs a byte array into an AXI data bus word.
 *
 * The helpers model standard little-endian AXI byte lanes: address bit offsets
 * select the target byte lane inside the bus word.
 */
uint64_t PackWriteData(uint64_t addr, const uint8_t *data, size_t beat_bytes,
                       size_t bus_bytes);

/** Extracts a read beat from an AXI data bus word into a byte array. */
void UnpackReadData(uint64_t addr, uint64_t bus_word, uint8_t *data,
                    size_t beat_bytes, size_t bus_bytes);

/**
 * Extracts WDATA/WSTRB into TLM data and byte-enable buffers.
 *
 * The byte-enable buffer uses 0xff for enabled bytes and 0x00 for disabled
 * bytes, matching TLM generic payload byte-enable convention.
 */
void ExtractWriteBeat(uint64_t addr, uint64_t bus_word, uint32_t strobe,
                      uint8_t *data, uint8_t *byte_enable, size_t beat_bytes,
                      size_t bus_bytes);

/** Aggregates beat responses by keeping the numerically strongest error. */
inline AxiResp AggregateResp(AxiResp current_resp, AxiResp new_resp) {
  return static_cast<AxiResp>(std::max(static_cast<uint8_t>(current_resp),
                                       static_cast<uint8_t>(new_resp)));
}

}  // namespace lv::axi
