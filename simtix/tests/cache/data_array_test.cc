// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cache/data_array.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "cache/packet.h"
#include "cache/policies.h"
#include "catch2/catch_session.hpp"
#include "catch2/catch_test_macros.hpp"
#include "systemc.h"

namespace {

using Byte = unsigned char;
using simtix::cache::DataArray;
using simtix::cache::Location;
using simtix::cache::Packet;
using simtix::cache::PacketType;
using simtix::cache::Param;
using simtix::cache::ReplacementPolicy;
using simtix::cache::WriteHitPolicy;

constexpr size_t kCacheSize = 128;
constexpr size_t kBlockSize = 16;
constexpr size_t kNumWays = 2;
constexpr Byte kReadFillByte = 0xEE;

struct VictimInfo {
  uint64_t address = 0;
  std::vector<Byte> data;
};

Param MakeParam(size_t cache_size, size_t block_size, size_t ways) {
  Param param;
  param.cache_size_bytes = cache_size;
  param.block_size_bytes = block_size;
  param.ways = ways;
  param.replacement_policy = ReplacementPolicy::kFIFO;
  param.write_hit_policy = WriteHitPolicy::kWriteBack;
  return param;
}

size_t SetIndex(const Param &param, uint64_t address) {
  return (address / param.block_size_bytes) % param.GetNumSets();
}

std::vector<Byte> Bytes(std::initializer_list<Byte> bytes) { return bytes; }

std::vector<Byte> RepeatedBytes(size_t size, Byte value) {
  return std::vector<Byte>(size, value);
}

std::vector<Byte> Sequence(size_t size, Byte first = 0) {
  std::vector<Byte> bytes(size);
  for (size_t i = 0; i < bytes.size(); ++i) {
    bytes[i] = static_cast<Byte>(first + i);
  }
  return bytes;
}

class PacketBuilder {
 public:
  PacketBuilder(PacketType type, uint64_t address, bool is_write)
      : payload_(std::make_unique<tlm::tlm_generic_payload>()) {
    payload_->set_address(address);
    payload_->set_command(is_write ? tlm::TLM_WRITE_COMMAND
                                   : tlm::TLM_READ_COMMAND);
    packet_.type = type;
    packet_.SetPayload(payload_.get());
  }

  Packet *packet() { return &packet_; }

  const std::vector<Byte> &data() const { return data_; }
  const std::optional<VictimInfo> &victim() const { return victim_; }

  PacketBuilder &SetLocation(Location location) {
    packet_.is_hit = true;
    packet_.location = location;
    return *this;
  }

  PacketBuilder &SetVictimDirty(uint64_t victim_address) {
    packet_.is_victim_dirty = true;
    packet_.victim_address = victim_address;
    return *this;
  }

  PacketBuilder &SetData(std::vector<Byte> data) {
    data_ = std::move(data);
    payload_->set_data_ptr(data_.data());
    payload_->set_data_length(data_.size());
    payload_->set_streaming_width(data_.size());
    return *this;
  }

  PacketBuilder &SetBuffer(size_t size, Byte fill = 0) {
    return SetData(RepeatedBytes(size, fill));
  }

  PacketBuilder &SetByteEnable(std::vector<Byte> byte_enable) {
    byte_enable_ = std::move(byte_enable);
    payload_->set_byte_enable_ptr(byte_enable_.data());
    payload_->set_byte_enable_length(byte_enable_.size());
    return *this;
  }

  PacketBuilder &SetVictim(std::optional<VictimInfo> victim) {
    victim_ = std::move(victim);
    return *this;
  }

  PacketBuilder(const PacketBuilder &) = delete;
  PacketBuilder &operator=(const PacketBuilder &) = delete;
  PacketBuilder(PacketBuilder &&) = default;
  PacketBuilder &operator=(PacketBuilder &&) = default;

 private:
  std::unique_ptr<tlm::tlm_generic_payload> payload_;
  std::vector<Byte> data_;
  std::vector<Byte> byte_enable_;
  std::optional<VictimInfo> victim_;
  Packet packet_{};
};

class ScopedDataArrayReportSuppression {
 public:
  ScopedDataArrayReportSuppression()
      : previous_(sc_core::sc_report_handler::set_actions(
            "DataArray", sc_core::SC_ERROR, sc_core::SC_DO_NOTHING)) {}

  ~ScopedDataArrayReportSuppression() {
    sc_core::sc_report_handler::set_actions("DataArray", sc_core::SC_ERROR,
                                            previous_);
  }

 private:
  sc_core::sc_actions previous_;
};

}  // namespace

namespace simtix::cache {

class DataArrayTester {
 public:
  explicit DataArrayTester(DataArray &dut) : dut_(dut) {}

  PacketBuilder Read(uint64_t address, Location location, size_t size,
                     Byte fill = 0, PacketType type = PacketType::kCoreReq) {
    PacketBuilder packet(type, address, false);
    packet.SetLocation(location).SetBuffer(size, fill);
    packet.SetVictim(Process(packet));
    return packet;
  }

  PacketBuilder ReadWithByteEnable(uint64_t address, Location location,
                                   size_t size, Byte fill,
                                   std::vector<Byte> byte_enable) {
    PacketBuilder packet(PacketType::kCoreReq, address, false);
    packet.SetLocation(location)
        .SetBuffer(size, fill)
        .SetByteEnable(std::move(byte_enable));
    packet.SetVictim(Process(packet));
    return packet;
  }

  PacketBuilder Write(uint64_t address, Location location,
                      std::vector<Byte> data,
                      PacketType type = PacketType::kCoreReq) {
    PacketBuilder packet(type, address, true);
    packet.SetLocation(location).SetData(std::move(data));
    packet.SetVictim(Process(packet));
    return packet;
  }

  PacketBuilder WriteWithByteEnable(uint64_t address, Location location,
                                    std::vector<Byte> data,
                                    std::vector<Byte> byte_enable) {
    PacketBuilder packet(PacketType::kCoreReq, address, true);
    packet.SetLocation(location)
        .SetData(std::move(data))
        .SetByteEnable(std::move(byte_enable));
    packet.SetVictim(Process(packet));
    return packet;
  }

  PacketBuilder Refill(uint64_t address, Location location,
                       std::vector<Byte> block, bool is_victim_dirty = false,
                       uint64_t victim_address = 0) {
    PacketBuilder packet(PacketType::kRefill, address, true);
    packet.SetLocation(location).SetData(std::move(block));
    if (is_victim_dirty) {
      packet.SetVictimDirty(victim_address);
    }
    packet.SetVictim(Process(packet));
    return packet;
  }

 private:
  std::optional<VictimInfo> Process(PacketBuilder &packet) {
    std::vector<Byte> victim_data(kBlockSize, 0);
    DataArray::VictimBuffer victim_buffer;
    victim_buffer.data = victim_data.data();
    victim_buffer.size = victim_data.size();

    const DataArray::ProcessResult result =
        dut_.Process(packet.packet(), &victim_buffer);
    REQUIRE(result.ok);
    if (!result.victim_generated) {
      return std::nullopt;
    }

    return VictimInfo{victim_buffer.address, std::move(victim_data)};
  }

  DataArray &dut_;
};

}  // namespace simtix::cache

using simtix::cache::DataArrayTester;

SCENARIO("Data array writes and reads bytes at a block offset",
         "[cache][data_array]") {
  DataArray data_array(MakeParam(kCacheSize, kBlockSize, kNumWays));
  DataArrayTester driver(data_array);
  const Location location{.set = 1, .way = 0};
  const uint64_t line_address = 0x20;
  const size_t write_offset = 5;
  const uint64_t write_address = line_address + write_offset;

  GIVEN("a line filled with known data") {
    auto expected = Sequence(kBlockSize, 0x10);
    driver.Refill(line_address, location, expected);

    WHEN("a write updates part of the line") {
      const auto patch = Bytes({0xA0, 0xA1, 0xA2});
      driver.Write(write_address, location, patch);
      std::copy(patch.begin(), patch.end(), expected.begin() + write_offset);

      THEN("a full-line read sees the updated bytes") {
        auto read =
            driver.Read(line_address, location, kBlockSize, kReadFillByte);
        CHECK(read.data() == expected);
      }
    }
  }
}

SCENARIO("Data array reads partial bytes at a block offset",
         "[cache][data_array]") {
  DataArray data_array(MakeParam(kCacheSize, kBlockSize, kNumWays));
  DataArrayTester driver(data_array);
  const Location location{.set = 2, .way = 0};
  const uint64_t line_address = 0x40;
  const size_t read_offset = 5;
  const uint64_t read_address = line_address + read_offset;
  const size_t read_size = 4;

  GIVEN("a line filled with non-uniform data") {
    const auto block = Sequence(kBlockSize, 0x80);
    driver.Refill(line_address, location, block);

    WHEN("a partial read starts inside the line") {
      auto read = driver.Read(read_address, location, read_size, kReadFillByte);

      THEN("the payload receives the bytes starting from the block offset") {
        CHECK(read.data() ==
              std::vector<Byte>(block.begin() + read_offset,
                                block.begin() + read_offset + read_size));
      }
    }
  }
}

SCENARIO("Data array keeps each cache location independent",
         "[cache][data_array]") {
  DataArray data_array(MakeParam(kCacheSize, kBlockSize, kNumWays));
  DataArrayTester driver(data_array);
  const Location line_a{.set = 2, .way = 0};
  const Location line_b{.set = 2, .way = 1};
  const uint64_t line_address = 0x40;

  GIVEN("both ways contain distinct data") {
    auto expected_a = Sequence(kBlockSize, 0x20);
    const auto expected_b = Sequence(kBlockSize, 0x80);
    driver.Refill(line_address, line_a, expected_a);
    driver.Refill(line_address, line_b, expected_b);

    WHEN("one way is partially updated") {
      const size_t write_offset = 4;
      const auto patch = Bytes({0xD0, 0xD1, 0xD2, 0xD3});
      driver.Write(line_address + write_offset, line_a, patch);
      std::copy(patch.begin(), patch.end(), expected_a.begin() + write_offset);

      THEN("the updated way contains the patch") {
        auto read_a =
            driver.Read(line_address, line_a, kBlockSize, kReadFillByte);
        CHECK(read_a.data() == expected_a);
      }

      AND_THEN("the other way keeps its original data") {
        auto read_b =
            driver.Read(line_address, line_b, kBlockSize, kReadFillByte);
        CHECK(read_b.data() == expected_b);
      }
    }
  }
}

SCENARIO("Data array byte-enable write updates selected lanes",
         "[cache][data_array]") {
  DataArray data_array(MakeParam(kCacheSize, kBlockSize, kNumWays));
  DataArrayTester driver(data_array);
  const Location location{.set = 0, .way = 1};
  const uint64_t line_address = 0x00;
  const size_t write_offset = 4;
  const uint64_t write_address = line_address + write_offset;

  GIVEN("a line filled with known data") {
    auto expected = Sequence(kBlockSize, 0x30);
    driver.Refill(line_address, location, expected);

    WHEN("a write uses a repeating byte-enable mask") {
      const auto patch = Bytes({0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5});
      driver.WriteWithByteEnable(write_address, location, patch,
                                 Bytes({TLM_BYTE_ENABLED, TLM_BYTE_DISABLED}));

      for (size_t i = 0; i < patch.size(); i += 2) {
        expected[write_offset + i] = patch[i];
      }

      THEN("only the enabled lanes are stored") {
        auto read =
            driver.Read(line_address, location, kBlockSize, kReadFillByte);
        CHECK(read.data() == expected);
      }
    }
  }
}

SCENARIO("Data array byte-enable write with non-repeating mask",
         "[cache][data_array]") {
  DataArray data_array(MakeParam(kCacheSize, kBlockSize, kNumWays));
  DataArrayTester driver(data_array);
  const Location location{.set = 0, .way = 1};
  const uint64_t line_address = 0x00;
  const size_t write_offset = 4;
  const uint64_t write_address = line_address + write_offset;

  GIVEN("a line filled with known data") {
    auto expected = Sequence(kBlockSize, 0x30);
    driver.Refill(line_address, location, expected);

    WHEN("a write uses one byte-enable lane per data byte") {
      const auto patch = Bytes({0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5});
      driver.WriteWithByteEnable(
          write_address, location, patch,
          Bytes({TLM_BYTE_DISABLED, TLM_BYTE_ENABLED, TLM_BYTE_ENABLED,
                 TLM_BYTE_DISABLED, TLM_BYTE_ENABLED, TLM_BYTE_DISABLED}));

      expected[write_offset + 1] = patch[1];
      expected[write_offset + 2] = patch[2];
      expected[write_offset + 4] = patch[4];

      THEN("only the individually enabled lanes are stored") {
        auto read =
            driver.Read(line_address, location, kBlockSize, kReadFillByte);
        CHECK(read.data() == expected);
      }
    }
  }
}

SCENARIO("Data array byte-enable read updates selected lanes",
         "[cache][data_array]") {
  DataArray data_array(MakeParam(kCacheSize, kBlockSize, kNumWays));
  DataArrayTester driver(data_array);
  const Location location{.set = 3, .way = 0};
  const uint64_t line_address = 0x60;
  const size_t read_offset = 4;
  const uint64_t read_address = line_address + read_offset;
  const size_t read_size = 6;

  GIVEN("a line filled with known data") {
    const auto block = Sequence(kBlockSize, 0x40);
    driver.Refill(line_address, location, block);

    WHEN("a read uses a repeating byte-enable mask") {
      auto read = driver.ReadWithByteEnable(
          read_address, location, read_size, kReadFillByte,
          Bytes({TLM_BYTE_DISABLED, TLM_BYTE_ENABLED}));

      THEN("only the enabled lanes are copied to the payload") {
        CHECK(read.data() == Bytes({kReadFillByte, block[read_offset + 1],
                                    kReadFillByte, block[read_offset + 3],
                                    kReadFillByte, block[read_offset + 5]}));
      }
    }
  }
}

SCENARIO("Data array replays read and write hits", "[cache][data_array]") {
  DataArray data_array(MakeParam(kCacheSize, kBlockSize, kNumWays));
  DataArrayTester driver(data_array);
  const Location location{.set = 0, .way = 0};
  const uint64_t line_address = 0x00;
  const size_t read_offset = 4;
  const uint64_t read_address = line_address + read_offset;
  const size_t read_size = 4;
  const size_t write_offset = 8;
  const uint64_t write_address = line_address + write_offset;

  GIVEN("a line filled with known data") {
    auto expected = Sequence(kBlockSize, 0x90);
    driver.Refill(line_address, location, expected);

    WHEN("a replay read and replay write access the line") {
      auto replay_read = driver.Read(read_address, location, read_size,
                                     kReadFillByte, PacketType::kReplay);
      const auto patch = Bytes({0xD0, 0xD1, 0xD2});
      auto replay_write =
          driver.Write(write_address, location, patch, PacketType::kReplay);
      std::copy(patch.begin(), patch.end(), expected.begin() + write_offset);

      THEN("the replay read returns data from the line") {
        CHECK(replay_read.packet()->type == PacketType::kReplay);
        CHECK_FALSE(replay_read.victim().has_value());
        CHECK(replay_read.data() ==
              std::vector<Byte>(expected.begin() + read_offset,
                                expected.begin() + read_offset + read_size));
      }

      AND_THEN("the replay write updates the line") {
        CHECK(replay_write.packet()->type == PacketType::kReplay);
        CHECK_FALSE(replay_write.victim().has_value());

        auto read =
            driver.Read(line_address, location, kBlockSize, kReadFillByte);
        CHECK(read.data() == expected);
      }
    }
  }
}

SCENARIO("Data array clean refill keeps the incoming payload",
         "[cache][data_array]") {
  DataArray data_array(MakeParam(kCacheSize, kBlockSize, kNumWays));
  DataArrayTester driver(data_array);
  const Location location{.set = 1, .way = 1};
  const uint64_t line_address = 0x20;

  GIVEN("a filled cache line") {
    const auto old_block = Sequence(kBlockSize, 0x50);
    const auto new_block = Sequence(kBlockSize, 0x70);
    driver.Refill(line_address, location, old_block);

    WHEN("a refill replaces the line without a dirty victim") {
      auto refill = driver.Refill(line_address, location, new_block);

      THEN("the refill payload still carries the incoming block") {
        CHECK_FALSE(refill.packet()->is_victim_dirty);
        CHECK_FALSE(refill.victim().has_value());
        CHECK(refill.data() == new_block);
      }

      AND_THEN("the data array stores the incoming block") {
        auto read =
            driver.Read(line_address, location, kBlockSize, kReadFillByte);
        CHECK(read.data() == new_block);
      }
    }
  }
}

SCENARIO("Data array dirty refill returns the evicted block",
         "[cache][data_array]") {
  const Param param = MakeParam(kCacheSize, kBlockSize, kNumWays);
  DataArray data_array(param);
  DataArrayTester driver(data_array);
  const uint64_t victim_address = 0x20;
  const uint64_t refill_address = 0x60;
  REQUIRE(SetIndex(param, victim_address) == SetIndex(param, refill_address));
  const Location location{.set = SetIndex(param, victim_address), .way = 1};

  GIVEN("a filled cache line") {
    const auto old_block = Sequence(kBlockSize, 0x50);
    const auto new_block = Sequence(kBlockSize, 0x70);
    driver.Refill(victim_address, location, old_block);

    WHEN("a dirty refill replaces the line") {
      auto refill = driver.Refill(refill_address, location, new_block, true,
                                  victim_address);

      THEN("the victim result carries the evicted block") {
        REQUIRE(refill.victim().has_value());
        CHECK(refill.packet()->GetTlmGp()->get_response_status() ==
              tlm::TLM_INCOMPLETE_RESPONSE);
        CHECK(refill.victim()->address == victim_address);
        CHECK(refill.victim()->data == old_block);
      }

      AND_THEN("the refill payload still carries the incoming block") {
        CHECK(refill.data() == new_block);
      }

      AND_THEN("the data array keeps the incoming block") {
        auto read =
            driver.Read(refill_address, location, kBlockSize, kReadFillByte);
        CHECK(read.data() == new_block);
      }
    }
  }
}

SCENARIO("Data array rejects invalid packet accesses", "[cache][data_array]") {
  ScopedDataArrayReportSuppression suppress_reports;
  DataArray data_array(MakeParam(kCacheSize, kBlockSize, kNumWays));
  const Location valid_location{.set = 0, .way = 0};
  const Location invalid_location{.set = 100, .way = 0};

  GIVEN("a read packet with an out-of-range cache location") {
    PacketBuilder read(PacketType::kCoreReq, 0x00, false);
    read.SetLocation(invalid_location).SetBuffer(4, kReadFillByte);

    WHEN("the packet is processed") {
      const DataArray::ProcessResult result = data_array.Process(read.packet());

      THEN("the failure is reported through the result and TLM status") {
        CHECK_FALSE(result.ok);
        CHECK_FALSE(result.victim_generated);
        CHECK(read.packet()->GetTlmGp()->get_response_status() ==
              tlm::TLM_GENERIC_ERROR_RESPONSE);
      }
    }
  }

  GIVEN("a read packet that crosses the cache block boundary") {
    PacketBuilder read(PacketType::kCoreReq, kBlockSize - 2, false);
    read.SetLocation(valid_location).SetBuffer(4, kReadFillByte);
    const auto before = read.data();

    WHEN("the packet is processed") {
      const DataArray::ProcessResult result = data_array.Process(read.packet());

      THEN("the access fails without modifying the read buffer") {
        CHECK_FALSE(result.ok);
        CHECK_FALSE(result.victim_generated);
        CHECK(read.data() == before);
        CHECK(read.packet()->GetTlmGp()->get_response_status() ==
              tlm::TLM_GENERIC_ERROR_RESPONSE);
      }
    }
  }

  GIVEN("a write packet with an invalid cache location") {
    PacketBuilder write(PacketType::kCoreReq, 0x00, true);
    write.SetLocation(invalid_location).SetData(Bytes({0xA0, 0xA1}));

    WHEN("the packet is processed") {
      const DataArray::ProcessResult result =
          data_array.Process(write.packet());

      THEN("the write failure is distinct from a clean no-victim result") {
        CHECK_FALSE(result.ok);
        CHECK_FALSE(result.victim_generated);
        CHECK(write.packet()->GetTlmGp()->get_response_status() ==
              tlm::TLM_GENERIC_ERROR_RESPONSE);
      }
    }
  }

  GIVEN("a byte read with a null destination") {
    PacketBuilder read(PacketType::kCoreReq, 0x00, false);
    read.SetLocation(valid_location).SetBuffer(4, kReadFillByte);

    WHEN("nonzero bytes are read") {
      const bool result = data_array.ReadBytes(read.packet(), nullptr, 4);

      THEN("the access fails through the runtime error path") {
        CHECK_FALSE(result);
        CHECK(read.packet()->GetTlmGp()->get_response_status() ==
              tlm::TLM_GENERIC_ERROR_RESPONSE);
      }
    }
  }

  GIVEN("a byte write with a null source") {
    PacketBuilder write(PacketType::kCoreReq, 0x00, true);
    write.SetLocation(valid_location).SetData(Bytes({0xA0, 0xA1}));

    WHEN("nonzero bytes are written") {
      const bool result = data_array.WriteBytes(write.packet(), nullptr, 2);

      THEN("the access fails through the runtime error path") {
        CHECK_FALSE(result);
        CHECK(write.packet()->GetTlmGp()->get_response_status() ==
              tlm::TLM_GENERIC_ERROR_RESPONSE);
      }
    }
  }

  GIVEN("a block read with a null destination") {
    WHEN("a whole block is read") {
      const bool result =
          data_array.ReadBlock(valid_location, nullptr, kBlockSize);

      THEN("the access fails through the runtime error path") {
        CHECK_FALSE(result);
      }
    }
  }
}

int sc_main(int argc, char *argv[]) { return Catch::Session().run(argc, argv); }
