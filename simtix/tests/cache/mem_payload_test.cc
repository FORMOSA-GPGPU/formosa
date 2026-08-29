// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cache/mem_payload.h"

#include <cstdint>
#include <initializer_list>
#include <vector>

#include "catch2/catch_session.hpp"
#include "catch2/catch_test_macros.hpp"
#include "systemc.h"

namespace {

using simtix::cache::MemPayload;

std::vector<uint8_t> Bytes(std::initializer_list<uint8_t> bytes) {
  return std::vector<uint8_t>(bytes);
}

}  // namespace

SCENARIO("MemPayload builds masked line writes from byte-enabled writes",
         "[cache][mem_payload]") {
  constexpr size_t kBlockSize = 16;
  std::vector<uint8_t> data = Bytes({0x10, 0x11, 0x12, 0x13});
  std::vector<uint8_t> byte_enable =
      Bytes({TLM_BYTE_ENABLED, TLM_BYTE_DISABLED});
  tlm::tlm_generic_payload src;
  src.set_command(tlm::TLM_WRITE_COMMAND);
  src.set_address(0x43);
  src.set_data_ptr(data.data());
  src.set_data_length(static_cast<unsigned int>(data.size()));
  src.set_streaming_width(2);
  src.set_byte_enable_ptr(byte_enable.data());
  src.set_byte_enable_length(static_cast<unsigned int>(byte_enable.size()));
  src.set_dmi_allowed(true);
  src.set_response_status(tlm::TLM_OK_RESPONSE);

  MemPayload payload;
  payload.InitStorage(kBlockSize);
  payload.InitLineWriteFrom(src, kBlockSize);

  CHECK(payload.gp.is_write());
  CHECK(payload.gp.get_address() == 0x40);
  CHECK(payload.gp.get_data_length() == kBlockSize);
  CHECK(payload.gp.get_streaming_width() == kBlockSize);
  CHECK(payload.gp.get_byte_enable_length() == kBlockSize);
  CHECK(payload.gp.is_dmi_allowed() == false);
  CHECK(payload.gp.get_response_status() == tlm::TLM_INCOMPLETE_RESPONSE);

  REQUIRE(payload.gp.get_data_ptr() == payload.buffer.data());
  REQUIRE(payload.gp.get_data_ptr() != data.data());
  REQUIRE(payload.gp.get_byte_enable_ptr() == payload.byte_enable.data());
  REQUIRE(payload.gp.get_byte_enable_ptr() != byte_enable.data());

  std::vector<uint8_t> expected_data(kBlockSize, 0);
  expected_data[3] = 0x10;
  expected_data[4] = 0x11;
  expected_data[5] = 0x12;
  expected_data[6] = 0x13;
  CHECK(payload.buffer == expected_data);

  std::vector<uint8_t> expected_byte_enable(kBlockSize, TLM_BYTE_DISABLED);
  expected_byte_enable[3] = TLM_BYTE_ENABLED;
  expected_byte_enable[5] = TLM_BYTE_ENABLED;
  CHECK(payload.byte_enable == expected_byte_enable);

  data[0] = 0xFE;
  byte_enable[0] = TLM_BYTE_DISABLED;
  CHECK(payload.buffer == expected_data);
  CHECK(payload.byte_enable == expected_byte_enable);
}

SCENARIO("MemPayload line writes enable unmasked source bytes",
         "[cache][mem_payload]") {
  constexpr size_t kBlockSize = 16;
  std::vector<uint8_t> data = Bytes({0x21, 0x22});
  tlm::tlm_generic_payload src;
  src.set_command(tlm::TLM_WRITE_COMMAND);
  src.set_address(0x8E);
  src.set_data_ptr(data.data());
  src.set_data_length(static_cast<unsigned int>(data.size()));
  src.set_streaming_width(static_cast<unsigned int>(data.size()));
  src.set_byte_enable_ptr(nullptr);
  src.set_byte_enable_length(0);

  MemPayload payload;
  payload.InitStorage(kBlockSize);
  payload.InitLineWriteFrom(src, kBlockSize);

  CHECK(payload.gp.get_address() == 0x80);
  CHECK(payload.gp.get_data_length() == kBlockSize);
  CHECK(payload.gp.get_streaming_width() == kBlockSize);
  CHECK(payload.gp.get_byte_enable_length() == kBlockSize);
  CHECK(payload.gp.get_byte_enable_ptr() == payload.byte_enable.data());
  CHECK(payload.gp.get_data_ptr() == payload.buffer.data());

  std::vector<uint8_t> expected_data(kBlockSize, 0);
  expected_data[14] = 0x21;
  expected_data[15] = 0x22;
  CHECK(payload.buffer == expected_data);

  std::vector<uint8_t> expected_byte_enable(kBlockSize, TLM_BYTE_DISABLED);
  expected_byte_enable[14] = TLM_BYTE_ENABLED;
  expected_byte_enable[15] = TLM_BYTE_ENABLED;
  CHECK(payload.byte_enable == expected_byte_enable);
}

int sc_main(int argc, char *argv[]) { return Catch::Session().run(argc, argv); }
