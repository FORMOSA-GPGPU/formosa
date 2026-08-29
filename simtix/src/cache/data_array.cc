// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "data_array.h"

#include <tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h>

#include <cassert>
#include <cstring>
#include <string>
#include <vector>

namespace simtix::cache {

namespace {

bool ReportInvalidAccess(const char *operation, const char *reason,
                         tlm::tlm_generic_payload *gp = nullptr) {
  if (gp != nullptr) {
    gp->set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
  }
  const std::string message =
      std::string(operation) + " rejected invalid packet: " + reason;
  SC_REPORT_ERROR("DataArray", message.c_str());
  return false;
}

/**
 * @brief Validate payload bounds for one block-local data-array access.
 *
 * @param operation Name of the attempted operation.
 * @param gp TLM payload used for the data-array access.
 * @param block_offset Byte offset inside the selected block.
 * @param block_size Cache block size in bytes.
 * @return true when the payload access is valid, false after reporting an
 * error otherwise.
 */
bool ValidatePayloadAccess(const char *operation, tlm::tlm_generic_payload *gp,
                           size_t block_offset, size_t block_size) {
  assert(gp != nullptr);

  const size_t data_length = gp->get_data_length();
  if (data_length != 0 && gp->get_data_ptr() == nullptr) {
    return ReportInvalidAccess(operation, "payload data pointer is null", gp);
  }

  const bool fits_in_block =
      data_length <= block_size && block_offset <= block_size - data_length;
  if (!fits_in_block) {
    return ReportInvalidAccess(operation,
                               "payload crosses cache block boundary", gp);
  }

  return true;
}

/**
 * @brief Validate that a packet location names an existing cache block.
 *
 * @param operation Name of the attempted operation.
 * @param location Cache set/way selected by the tag array.
 * @param num_sets Number of sets in the data array.
 * @param ways Number of ways per set.
 * @return true when `location` is in range, false after reporting an error.
 */
bool ValidateLocation(const char *operation, Location location, size_t num_sets,
                      size_t ways, tlm::tlm_generic_payload *gp = nullptr) {
  if (location.set >= num_sets || location.way >= ways) {
    return ReportInvalidAccess(operation, "cache location is out of range", gp);
  }

  return true;
}

/**
 * @brief Validate dirty-victim refill requirements.
 *
 * @param operation Name of the attempted operation.
 * @param packet Packet being processed.
 * @param block_offset Byte offset inside the selected block.
 * @param data_length Payload data length in bytes.
 * @param block_size Cache block size in bytes.
 * @return true when the dirty-victim refill is valid, false after reporting an
 * error otherwise.
 */
bool ValidateDirtyVictimRefill(const char *operation, const Packet *packet,
                               size_t block_offset, size_t data_length,
                               size_t block_size) {
  assert(packet != nullptr);

  const bool valid_refill = packet->type == PacketType::kRefill &&
                            data_length == block_size && block_offset == 0;
  if (!valid_refill) {
    return ReportInvalidAccess(
        operation, "dirty victim refill must be a block-aligned whole block",
        packet->GetTlmGp());
  }

  return true;
}

void Load(uint8_t *buf, tlm::tlm_generic_payload *gp) {
  auto gp_data_ptr = gp->get_data_ptr();
  auto gp_data_length = gp->get_data_length();
  auto gp_byte_enable_ptr = gp->get_byte_enable_ptr();
  auto gp_byte_enable_length = gp->get_byte_enable_length();

  if (gp_byte_enable_ptr != nullptr && gp_byte_enable_length > 0) {
    // Byte-enable read
    for (size_t i = 0; i < gp_data_length; i++) {
      if (gp_byte_enable_ptr[i % gp_byte_enable_length] == TLM_BYTE_ENABLED) {
        gp_data_ptr[i] = buf[i];
      }
    }
  } else {
    // Normal read
    std::memcpy(gp_data_ptr, buf, gp_data_length);
  }
}

void Store(uint8_t *buf, tlm::tlm_generic_payload *gp) {
  auto data_ptr = gp->get_data_ptr();
  auto data_length = gp->get_data_length();
  auto byte_enable_ptr = gp->get_byte_enable_ptr();
  auto byte_enable_length = gp->get_byte_enable_length();

  if (byte_enable_ptr != nullptr && byte_enable_length > 0) {
    // Byte-enable write
    for (size_t i = 0; i < data_length; i++) {
      if (byte_enable_ptr[i % byte_enable_length] == TLM_BYTE_ENABLED) {
        buf[i] = data_ptr[i];
      }
    }
  } else {
    // Normal write
    std::memcpy(buf, data_ptr, data_length);
  }
}

}  // namespace

DataArray::DataArray(const Param &p) : config_(p), num_sets_(p.GetNumSets()) {
  // Initialize the data array based on the cache configuration
  size_t total_size = num_sets_ * config_.ways * config_.block_size_bytes;
  data_array_.resize(total_size, 0);  // Initialize with zeros
}

uint8_t *DataArray::GetBlockData(Location loc) {
  const size_t block_index =
      (loc.set * config_.ways + loc.way) * config_.block_size_bytes;
  return &data_array_[block_index];
}

DataArray::ProcessResult DataArray::Process(Packet *packet,
                                            VictimBuffer *victim_buffer) {
  assert(packet != nullptr);
  // Only these packet types should access data array
  assert(packet->type == PacketType::kCoreReq ||
         packet->type == PacketType::kRefill ||
         packet->type == PacketType::kReplay);
  // Data array should only be accessed on cache hit
  assert(packet->is_hit);

  if (packet->is_write()) {
    // Write hit
    return Write(packet, victim_buffer);
  } else {
    // Read hit
    return {.ok = Read(packet), .victim_generated = false};
  }
}

bool DataArray::ReadBytes(Packet *packet, uint8_t *dst, size_t size) {
  assert(packet != nullptr);
  assert(packet->is_hit);

  const size_t block_offset =
      packet->GetTlmGp()->get_address() % config_.block_size_bytes;
  tlm::tlm_generic_payload *gp = packet->GetTlmGp();
  if (size != 0 && dst == nullptr) {
    return ReportInvalidAccess("ReadBytes", "destination pointer is null", gp);
  }
  const bool fits_in_block = size <= config_.block_size_bytes &&
                             block_offset <= config_.block_size_bytes - size;
  if (!fits_in_block) {
    return ReportInvalidAccess("ReadBytes",
                               "payload crosses cache block boundary", gp);
  }
  if (!ValidateLocation("ReadBytes", packet->location, num_sets_, config_.ways,
                        gp)) {
    return false;
  }

  uint8_t *block_data = GetBlockData(packet->location) + block_offset;
  std::memcpy(dst, block_data, size);
  return true;
}

bool DataArray::WriteBytes(Packet *packet, const uint8_t *src, size_t size) {
  assert(packet != nullptr);
  assert(packet->is_hit);

  const size_t block_offset =
      packet->GetTlmGp()->get_address() % config_.block_size_bytes;
  tlm::tlm_generic_payload *gp = packet->GetTlmGp();
  if (size != 0 && src == nullptr) {
    return ReportInvalidAccess("WriteBytes", "source pointer is null", gp);
  }
  const bool fits_in_block = size <= config_.block_size_bytes &&
                             block_offset <= config_.block_size_bytes - size;
  if (!fits_in_block) {
    return ReportInvalidAccess("WriteBytes",
                               "payload crosses cache block boundary", gp);
  }
  if (!ValidateLocation("WriteBytes", packet->location, num_sets_, config_.ways,
                        gp)) {
    return false;
  }

  uint8_t *block_data = GetBlockData(packet->location) + block_offset;
  std::memcpy(block_data, src, size);
  return true;
}

bool DataArray::ReadBlock(Location location, uint8_t *dst, size_t size) {
  if (size != config_.block_size_bytes) {
    return ReportInvalidAccess("ReadBlock",
                               "block read size must match cache block size");
  }
  if (size != 0 && dst == nullptr) {
    return ReportInvalidAccess("ReadBlock", "destination pointer is null");
  }
  if (!ValidateLocation("ReadBlock", location, num_sets_, config_.ways)) {
    return false;
  }

  std::memcpy(dst, GetBlockData(location), size);
  return true;
}

/**
 * @brief Write packet payload data into the selected cache block.
 *
 * @param packet The packet containing the data to write and victim info if
 * applicable
 * @return true if the packet has a dirty victim, false otherwise.
 */
DataArray::ProcessResult DataArray::Write(Packet *packet,
                                          VictimBuffer *victim_buffer) {
  tlm::tlm_generic_payload *gp = packet->GetTlmGp();
  const size_t block_offset = gp->get_address() % config_.block_size_bytes;
  const size_t gp_data_length = gp->get_data_length();

  if (!ValidatePayloadAccess("Write", gp, block_offset,
                             config_.block_size_bytes) ||
      !ValidateLocation("Write", packet->location, num_sets_, config_.ways,
                        gp)) {
    return {.ok = false, .victim_generated = false};
  }

  uint8_t *block_data = GetBlockData(packet->location) + block_offset;

  // Data Array is simulated as a 1RW SRAM with read-before-write behavior
  // If the packet is a write with victim dirty, we need to read the old data
  // out first, then write the new data in
  if (packet->is_victim_dirty) {
    if (!ValidateDirtyVictimRefill("Write", packet, block_offset,
                                   gp_data_length, config_.block_size_bytes)) {
      return {.ok = false, .victim_generated = false};
    }

    assert(victim_buffer != nullptr);
    assert(victim_buffer->data != nullptr);
    assert(victim_buffer->size == config_.block_size_bytes);
    victim_buffer->address = packet->victim_address;
    std::memcpy(victim_buffer->data, block_data, config_.block_size_bytes);
  }

  Store(block_data, gp);

  return {.ok = true, .victim_generated = packet->is_victim_dirty};
}

/**
 * @brief Read bytes from the selected cache block into the packet payload.
 *
 * @param packet The packet to fill with the read data
 */
bool DataArray::Read(Packet *packet) {
  tlm::tlm_generic_payload *gp = packet->GetTlmGp();
  const size_t block_offset = gp->get_address() % config_.block_size_bytes;

  if (!ValidatePayloadAccess("Read", gp, block_offset,
                             config_.block_size_bytes) ||
      !ValidateLocation("Read", packet->location, num_sets_, config_.ways,
                        gp)) {
    return false;
  }

  uint8_t *block_data = GetBlockData(packet->location) + block_offset;

  Load(block_data, gp);
  return true;
}

}  // namespace simtix::cache
