/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/mm/static.h>
#include <tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace simtix::cache {

struct MemPayload {
  tlm::tlm_generic_payload gp;
  std::vector<uint8_t> buffer;
  std::vector<uint8_t> byte_enable;

  /**
   * @brief Allocate fixed-size backing storage for pooled payload reuse.
   *
   * @param payload_size Number of bytes carried by memory-side transactions.
   */
  void InitStorage(size_t payload_size) {
    assert(payload_size > 0);
    buffer.resize(payload_size);
    byte_enable.resize(payload_size);
    gp.set_mm(lv::mm::Static);
    Reset();
  }

  /**
   * @brief Reset payload contents and detach the TLM GP from owned storage.
   */
  void Reset() {
    std::fill(buffer.begin(), buffer.end(), 0);
    std::fill(byte_enable.begin(), byte_enable.end(), TLM_BYTE_DISABLED);
    gp.set_command(tlm::TLM_IGNORE_COMMAND);
    gp.set_address(0);
    gp.set_data_ptr(nullptr);
    gp.set_data_length(0);
    gp.set_streaming_width(0);
    gp.set_byte_enable_ptr(nullptr);
    gp.set_byte_enable_length(0);
    gp.set_dmi_allowed(false);
    gp.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
  }

  /**
   * @brief Initialize a block read request with owned response storage.
   *
   * @param address Memory address to read.
   * @param payload_size Number of bytes carried by the response data.
   */
  void InitRead(uint64_t address, size_t payload_size) {
    Reset();
    assert(payload_size > 0);
    assert(buffer.size() == payload_size);
    assert(byte_enable.size() == payload_size);
    gp.set_command(tlm::TLM_READ_COMMAND);
    gp.set_address(address);
    gp.set_data_ptr(buffer.data());
    gp.set_data_length(static_cast<unsigned int>(buffer.size()));
    gp.set_streaming_width(static_cast<unsigned int>(buffer.size()));
    gp.set_byte_enable_ptr(nullptr);
    gp.set_byte_enable_length(0);
    gp.set_dmi_allowed(false);
    gp.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
  }

  /**
   * @brief Initialize a block-aligned whole-line write request.
   *
   * @param address Block-aligned memory address to write.
   * @param payload_size Number of bytes carried by the write transaction.
   */
  void InitLineWrite(uint64_t address, size_t payload_size) {
    Reset();
    assert(payload_size > 0);
    assert(address % payload_size == 0);
    assert(buffer.size() == payload_size);
    assert(byte_enable.size() == payload_size);

    std::fill(byte_enable.begin(), byte_enable.end(), TLM_BYTE_ENABLED);
    gp.set_command(tlm::TLM_WRITE_COMMAND);
    gp.set_address(address);
    gp.set_data_ptr(buffer.data());
    gp.set_data_length(static_cast<unsigned int>(buffer.size()));
    gp.set_streaming_width(static_cast<unsigned int>(buffer.size()));
    gp.set_byte_enable_ptr(byte_enable.data());
    gp.set_byte_enable_length(static_cast<unsigned int>(byte_enable.size()));
    gp.set_dmi_allowed(false);
    gp.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
  }

  /**
   * @brief Initialize a block-aligned write request from a partial write.
   *
   * @param src Source write transaction to copy into one cache-line request.
   * @param payload_size Number of bytes carried by the write transaction.
   */
  void InitLineWriteFrom(const tlm::tlm_generic_payload &src,
                         size_t payload_size) {
    Reset();
    assert(src.is_write());
    assert(payload_size > 0);
    assert(buffer.size() == payload_size);
    assert(byte_enable.size() == payload_size);

    const uint64_t address = src.get_address();
    const size_t block_offset = address % payload_size;
    const unsigned int data_length = src.get_data_length();
    const unsigned char *data_ptr = src.get_data_ptr();
    assert(data_ptr != nullptr || data_length == 0);
    assert(block_offset <= payload_size);
    assert(data_length <= payload_size - block_offset);

    if (data_length != 0) {
      std::memcpy(buffer.data() + block_offset, data_ptr, data_length);
    }

    const unsigned int src_byte_enable_length = src.get_byte_enable_length();
    const unsigned char *src_byte_enable_ptr = src.get_byte_enable_ptr();
    assert(src_byte_enable_ptr != nullptr || src_byte_enable_length == 0);
    assert(src_byte_enable_ptr == nullptr || src_byte_enable_length != 0);
    for (size_t i = 0; i < data_length; ++i) {
      const bool enabled =
          src_byte_enable_ptr == nullptr ||
          src_byte_enable_ptr[i % src_byte_enable_length] == TLM_BYTE_ENABLED;
      byte_enable[block_offset + i] =
          enabled ? TLM_BYTE_ENABLED : TLM_BYTE_DISABLED;
    }

    gp.set_command(tlm::TLM_WRITE_COMMAND);
    gp.set_address(address - block_offset);
    gp.set_data_ptr(buffer.data());
    gp.set_data_length(static_cast<unsigned int>(buffer.size()));
    gp.set_streaming_width(static_cast<unsigned int>(buffer.size()));
    gp.set_byte_enable_ptr(byte_enable.data());
    gp.set_byte_enable_length(static_cast<unsigned int>(byte_enable.size()));
    gp.set_dmi_allowed(false);
    gp.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
  }

  /**
   * @brief Treat a completed memory read as a cache-line refill write.
   */
  void PrepareRefillWrite() {
    assert(gp.is_read());
    assert(gp.get_data_ptr() == buffer.data());
    assert(gp.get_data_length() == buffer.size());
    gp.set_command(tlm::TLM_WRITE_COMMAND);
  }
};

}  // namespace simtix::cache
