// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/axi/axi_helpers.h>

#include <stdexcept>

namespace lv::axi {

uint32_t BytesToSizeCode(size_t num_bytes) {
  if (num_bytes == 0 || (num_bytes & (num_bytes - 1)) != 0) {
    throw std::invalid_argument("AXI beat size must be a power of two");
  }
  uint32_t code = 0;
  while ((size_t{1} << code) < num_bytes) {
    ++code;
  }
  return code;
}

AxiResp AxiRespFromTlmStatus(tlm::tlm_response_status status) {
  switch (status) {
    case tlm::TLM_OK_RESPONSE:
      return kOkayResp;
    case tlm::TLM_ADDRESS_ERROR_RESPONSE:
      return kDecErrResp;
    case tlm::TLM_COMMAND_ERROR_RESPONSE:
    case tlm::TLM_GENERIC_ERROR_RESPONSE:
    case tlm::TLM_BURST_ERROR_RESPONSE:
    default:
      return kSlvErrResp;
  }
}

tlm::tlm_response_status TlmStatusFromAxiResp(AxiResp resp) {
  switch (resp) {
    case kOkayResp:
    case kExOkayResp:
      return tlm::TLM_OK_RESPONSE;
    case kDecErrResp:
      return tlm::TLM_ADDRESS_ERROR_RESPONSE;
    case kSlvErrResp:
    default:
      return tlm::TLM_GENERIC_ERROR_RESPONSE;
  }
}

uint32_t BuildWriteStrobe(uint64_t addr, size_t beat_bytes, size_t bus_bytes) {
  const auto lane = static_cast<size_t>(addr % bus_bytes);
  uint32_t strobe = 0;
  for (size_t i = 0; i < beat_bytes; ++i) {
    strobe |= (1u << (lane + i));
  }
  return strobe;
}

uint64_t PackWriteData(uint64_t addr, const uint8_t *data, size_t beat_bytes,
                       size_t bus_bytes) {
  const auto lane = static_cast<size_t>(addr % bus_bytes);
  uint64_t packed = 0;
  for (size_t i = 0; i < beat_bytes; ++i) {
    packed |= (static_cast<uint64_t>(data[i]) << ((lane + i) * 8));
  }
  return packed;
}

void UnpackReadData(uint64_t addr, uint64_t bus_word, uint8_t *data,
                    size_t beat_bytes, size_t bus_bytes) {
  const auto lane = static_cast<size_t>(addr % bus_bytes);
  for (size_t i = 0; i < beat_bytes; ++i) {
    data[i] = static_cast<uint8_t>((bus_word >> ((lane + i) * 8)) & 0xffu);
  }
}

void ExtractWriteBeat(uint64_t addr, uint64_t bus_word, uint32_t strobe,
                      uint8_t *data, uint8_t *byte_enable, size_t beat_bytes,
                      size_t bus_bytes) {
  const auto lane = static_cast<size_t>(addr % bus_bytes);
  for (size_t i = 0; i < beat_bytes; ++i) {
    data[i] = static_cast<uint8_t>((bus_word >> ((lane + i) * 8)) & 0xffu);
    byte_enable[i] = ((strobe >> (lane + i)) & 0x1u) != 0 ? 0xffu : 0x00u;
  }
}

}  // namespace lv::axi
