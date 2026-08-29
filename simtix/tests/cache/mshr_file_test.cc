// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cache/mshr_file.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "cache/packet.h"
#include "catch2/catch_session.hpp"
#include "catch2/catch_test_macros.hpp"

namespace {

using simtix::cache::MshrCore;
using simtix::cache::Packet;
using simtix::cache::PacketType;
using simtix::cache::Param;
using simtix::cache::VictimReservation;
using Status = simtix::cache::MshrCore::ProbeResult::Status;

Param MakeParam(size_t block_size, size_t mshr_entries,
                size_t mshr_subentries) {
  Param param;
  param.block_size_bytes = block_size;
  param.mshr_entries = mshr_entries;
  param.mshr_subentries = mshr_subentries;
  return param;
}

class PacketFactory {
 public:
  Packet *ReadMiss(uint64_t address, size_t data_length = 0) {
    return MakePacket(PacketType::kCoreReq, address, false, data_length);
  }

  Packet *Refill(uint64_t address) {
    return MakePacket(PacketType::kRefill, address, false);
  }

 private:
  Packet *MakePacket(PacketType type, uint64_t address, bool is_write,
                     size_t data_length = 0) {
    auto payload = std::make_unique<tlm::tlm_generic_payload>();
    payload->set_address(address);
    payload->set_command(is_write ? tlm::TLM_WRITE_COMMAND
                                  : tlm::TLM_READ_COMMAND);
    if (data_length > 0) {
      auto data = std::make_unique<std::vector<uint8_t>>(data_length, 0xA5);
      payload->set_data_ptr(data->data());
      payload->set_data_length(data->size());
      payload->set_streaming_width(data->size());
      payload_data_.push_back(std::move(data));
    }

    auto packet = std::make_unique<Packet>();
    packet->unique_id = next_unique_id_++;
    packet->type = type;
    packet->SetPayload(payload.get());

    Packet *packet_ptr = packet.get();
    payloads_.push_back(std::move(payload));
    packets_.push_back(std::move(packet));
    return packet_ptr;
  }

  size_t next_unique_id_ = 0;
  std::vector<std::unique_ptr<tlm::tlm_generic_payload>> payloads_;
  std::vector<std::unique_ptr<std::vector<uint8_t>>> payload_data_;
  std::vector<std::unique_ptr<Packet>> packets_;
};

MshrCore::PrimaryMissAllocation AllocatePrimaryMissWithId(MshrCore &mshr,
                                                          Packet *packet) {
  auto probe = mshr.ProbeReadMiss(*packet);
  REQUIRE(probe.status == Status::kAcceptablePrimaryMiss);
  static size_t next_reservation = 0;
  return mshr.AllocatePrimaryMiss(
      probe.entry, packet,
      VictimReservation({next_reservation++, next_reservation}));
}

uint64_t AllocatePrimaryMiss(MshrCore &mshr, Packet *packet) {
  return AllocatePrimaryMissWithId(mshr, packet).read_address;
}

void AddSecondaryMiss(MshrCore &mshr, Packet *packet) {
  auto probe = mshr.ProbeReadMiss(*packet);
  REQUIRE(probe.status == Status::kAcceptableSecondaryMiss);

  mshr.AddSecondaryMiss(probe.entry, packet);
}

}  // namespace

SCENARIO("MSHR primary miss owns its victim reservation until refill",
         "[cache][mshr][reservation]") {
  PacketFactory packets;
  MshrCore mshr(MakeParam(16, 1, 1));
  Packet *miss = packets.ReadMiss(0x24, 4);
  auto probe = mshr.ProbeReadMiss(*miss);
  REQUIRE(probe.status == Status::kAcceptablePrimaryMiss);

  const simtix::cache::VictimReservationId reservation_id{3, 7};
  auto allocation = mshr.AllocatePrimaryMiss(probe.entry, miss,
                                             VictimReservation(reservation_id));

  CHECK(allocation.read_address == 0x20);
  CHECK(allocation.mshr_id.generation != 0);
  auto reservation = mshr.TakeVictimReservation(allocation.mshr_id);
  CHECK(reservation.id() == reservation_id);

  mshr.NotifyRefill(allocation.mshr_id);
  REQUIRE(mshr.HasReplayPacket());
  CHECK(mshr.PopReplayPacket() == miss);
}

SCENARIO("MSHR core allocates primary misses", "[cache][mshr]") {
  PacketFactory packets;
  const size_t block_size = 32;
  MshrCore mshr(MakeParam(block_size, 2, 2));

  GIVEN("an empty MSHR") {
    const uint64_t miss_address = 0x3B;
    const unsigned int miss_data_length = 4;
    const uint64_t read_address = 0x20;
    const uint64_t same_line_address = 0x2F;
    auto miss = packets.ReadMiss(miss_address, miss_data_length);

    WHEN("a read miss probes the MSHR") {
      auto probe = mshr.ProbeReadMiss(*miss);

      THEN("the miss can be accepted as a primary miss") {
        REQUIRE(probe.status == Status::kAcceptablePrimaryMiss);
        REQUIRE(probe.entry != nullptr);
      }

      AND_WHEN("the primary miss is allocated") {
        const uint64_t downstream_address = AllocatePrimaryMiss(mshr, miss);

        THEN("the downstream request is a block-aligned read") {
          CHECK(miss->GetTlmGp()->get_address() == miss_address);
          CHECK(miss->GetTlmGp()->get_data_length() == miss_data_length);
          CHECK(downstream_address == read_address);
        }

        AND_THEN("another request to the same line is a secondary miss") {
          auto same_line_miss = packets.ReadMiss(same_line_address);
          auto same_line_probe = mshr.ProbeReadMiss(*same_line_miss);

          CHECK(mshr.CanAcceptReadMiss(same_line_address));
          CHECK(same_line_probe.status == Status::kAcceptableSecondaryMiss);
          CHECK(same_line_probe.entry == probe.entry);
        }
      }
    }
  }
}

SCENARIO("MSHR core merges secondary misses and replays them on refill",
         "[cache][mshr]") {
  PacketFactory packets;
  const size_t block_size = 16;
  MshrCore mshr(MakeParam(block_size, 1, 2));

  GIVEN("a primary miss has been allocated") {
    const uint64_t primary_address = 0x20;
    const unsigned int primary_data_length = 4;
    auto primary = packets.ReadMiss(primary_address, primary_data_length);
    auto primary_probe = mshr.ProbeReadMiss(*primary);
    REQUIRE(primary_probe.status == Status::kAcceptablePrimaryMiss);

    Packet *primary_packet = primary;
    const size_t primary_unique_id = primary->unique_id;
    const auto refill = AllocatePrimaryMissWithId(mshr, primary);

    AND_GIVEN("a secondary miss targets the same line") {
      const uint64_t secondary_address = 0x2C;
      const unsigned int secondary_data_length = 8;
      auto secondary =
          packets.ReadMiss(secondary_address, secondary_data_length);
      auto secondary_probe = mshr.ProbeReadMiss(*secondary);
      REQUIRE(secondary_probe.status == Status::kAcceptableSecondaryMiss);
      REQUIRE(secondary_probe.entry == primary_probe.entry);

      Packet *secondary_packet = secondary;
      const size_t secondary_unique_id = secondary->unique_id;
      mshr.AddSecondaryMiss(secondary_probe.entry, secondary);

      WHEN("the line is refilled") {
        [[maybe_unused]] auto reservation =
            mshr.TakeVictimReservation(refill.mshr_id);
        mshr.NotifyRefill(refill.mshr_id);

        THEN("the entry rejects same-line misses while replaying") {
          auto same_line_miss = packets.ReadMiss(0x28);
          auto same_line_probe = mshr.ProbeReadMiss(*same_line_miss);

          CHECK(mshr.HasReplayPacket());
          CHECK_FALSE(mshr.CanAcceptReadMiss(0x28));
          CHECK(same_line_probe.status == Status::kReplaying);
          CHECK(same_line_probe.entry == nullptr);
        }

        AND_THEN("the replaying entry still occupies MSHR capacity") {
          auto different_line_miss = packets.ReadMiss(0x40);
          auto different_line_probe = mshr.ProbeReadMiss(*different_line_miss);

          CHECK_FALSE(mshr.CanAcceptReadMiss(0x40));
          CHECK(different_line_probe.status == Status::kMshrFull);
          CHECK(different_line_probe.entry == nullptr);
        }

        AND_WHEN("the first replay packet is popped") {
          Packet *first_replay = mshr.PopReplayPacket();

          THEN("the primary miss enters replay and the entry stays occupied") {
            REQUIRE(first_replay == primary_packet);
            CHECK(first_replay->type == PacketType::kReplay);
            CHECK(first_replay->unique_id == primary_unique_id);
            CHECK(first_replay->GetTlmGp()->get_address() == primary_address);
            CHECK(first_replay->GetTlmGp()->get_data_length() ==
                  primary_data_length);
            CHECK(mshr.HasReplayPacket());

            auto same_line_miss = packets.ReadMiss(0x28);
            auto same_line_probe = mshr.ProbeReadMiss(*same_line_miss);
            CHECK_FALSE(mshr.CanAcceptReadMiss(0x28));
            CHECK(same_line_probe.status == Status::kReplaying);
            CHECK(same_line_probe.entry == nullptr);

            auto different_line_miss = packets.ReadMiss(0x40);
            auto different_line_probe =
                mshr.ProbeReadMiss(*different_line_miss);
            CHECK_FALSE(mshr.CanAcceptReadMiss(0x40));
            CHECK(different_line_probe.status == Status::kMshrFull);
            CHECK(different_line_probe.entry == nullptr);
          }

          AND_WHEN("the final replay packet is popped") {
            Packet *second_replay = mshr.PopReplayPacket();

            THEN("the secondary miss enters replay and the entry is released") {
              REQUIRE(second_replay == secondary_packet);
              CHECK(second_replay->type == PacketType::kReplay);
              CHECK(second_replay->unique_id == secondary_unique_id);
              CHECK(second_replay->GetTlmGp()->get_address() ==
                    secondary_address);
              CHECK(second_replay->GetTlmGp()->get_data_length() ==
                    secondary_data_length);
              CHECK_FALSE(mshr.HasReplayPacket());

              auto next_miss = packets.ReadMiss(primary_address);
              auto next_probe = mshr.ProbeReadMiss(*next_miss);

              CHECK(mshr.CanAcceptReadMiss(primary_address));
              REQUIRE(next_probe.status == Status::kAcceptablePrimaryMiss);
              CHECK(next_probe.entry == primary_probe.entry);

              const uint64_t downstream_address =
                  AllocatePrimaryMiss(mshr, next_miss);
              CHECK(downstream_address == primary_address);
            }
          }
        }
      }
    }
  }
}

SCENARIO("MSHR core drains one active replay entry at a time",
         "[cache][mshr]") {
  PacketFactory packets;
  const size_t block_size = 16;
  MshrCore mshr(MakeParam(block_size, 2, 2));

  GIVEN("two entries each have primary and secondary misses") {
    auto first_primary = packets.ReadMiss(0x00);
    auto first_secondary = packets.ReadMiss(0x04);
    auto second_primary = packets.ReadMiss(0x10);
    auto second_secondary = packets.ReadMiss(0x14);

    const auto first_refill = AllocatePrimaryMissWithId(mshr, first_primary);
    AddSecondaryMiss(mshr, first_secondary);
    const auto second_refill = AllocatePrimaryMissWithId(mshr, second_primary);
    AddSecondaryMiss(mshr, second_secondary);

    WHEN("both entries are refilled before the first entry drains") {
      [[maybe_unused]] auto first_reservation =
          mshr.TakeVictimReservation(first_refill.mshr_id);
      mshr.NotifyRefill(first_refill.mshr_id);
      [[maybe_unused]] auto second_reservation =
          mshr.TakeVictimReservation(second_refill.mshr_id);
      mshr.NotifyRefill(second_refill.mshr_id);

      THEN("the replay order is grouped by notify-refill entry") {
        CHECK(mshr.PopReplayPacket() == first_primary);
        CHECK(first_primary->type == PacketType::kReplay);
        CHECK(mshr.PopReplayPacket() == first_secondary);
        CHECK(first_secondary->type == PacketType::kReplay);
        CHECK(mshr.PopReplayPacket() == second_primary);
        CHECK(second_primary->type == PacketType::kReplay);
        CHECK(mshr.PopReplayPacket() == second_secondary);
        CHECK(second_secondary->type == PacketType::kReplay);
        CHECK_FALSE(mshr.HasReplayPacket());
      }
    }
  }
}

SCENARIO("MSHR core reports sub-entry capacity separately", "[cache][mshr]") {
  PacketFactory packets;
  const size_t block_size = 16;
  MshrCore mshr(MakeParam(block_size, 2, 2));

  GIVEN("one line already has the maximum number of sub-entries") {
    const uint64_t primary_address = 0x30;
    const uint64_t secondary_address = 0x3C;
    const uint64_t overflow_address = 0x3F;
    const uint64_t different_line_address = 0x40;
    AllocatePrimaryMiss(mshr, packets.ReadMiss(primary_address));
    AddSecondaryMiss(mshr, packets.ReadMiss(secondary_address));

    WHEN("another miss targets the same line") {
      auto overflow = packets.ReadMiss(overflow_address);
      auto probe = mshr.ProbeReadMiss(*overflow);

      THEN("the probe is rejected as sub-entry full") {
        CHECK(probe.status == Status::kSubentryFull);
        CHECK(probe.entry == nullptr);
      }
    }

    AND_WHEN("a miss targets a different line") {
      auto different_line = packets.ReadMiss(different_line_address);
      auto probe = mshr.ProbeReadMiss(*different_line);

      THEN("the free MSHR entry can still accept it as a primary miss") {
        CHECK(probe.status == Status::kAcceptablePrimaryMiss);
        CHECK(probe.entry != nullptr);
      }
    }
  }
}

SCENARIO("MSHR core reports entry capacity separately", "[cache][mshr]") {
  PacketFactory packets;
  const size_t block_size = 16;
  MshrCore mshr(MakeParam(block_size, 2, 2));

  GIVEN("all MSHR entries are occupied by different lines") {
    const uint64_t first_line_address = 0x00;
    const uint64_t second_line_address = 0x10;
    const uint64_t different_line_address = 0x20;
    const uint64_t same_line_address = 0x0C;
    AllocatePrimaryMiss(mshr, packets.ReadMiss(first_line_address));
    AllocatePrimaryMiss(mshr, packets.ReadMiss(second_line_address));

    WHEN("a new line misses") {
      auto different_line = packets.ReadMiss(different_line_address);
      auto probe = mshr.ProbeReadMiss(*different_line);

      THEN("the probe is rejected as MSHR full") {
        CHECK(probe.status == Status::kMshrFull);
        CHECK(probe.entry == nullptr);
      }
    }

    AND_WHEN("an occupied line receives a secondary miss") {
      auto same_line = packets.ReadMiss(same_line_address);
      auto probe = mshr.ProbeReadMiss(*same_line);

      THEN("the secondary miss can still be accepted") {
        CHECK(probe.status == Status::kAcceptableSecondaryMiss);
        CHECK(probe.entry != nullptr);
      }
    }
  }
}

int sc_main(int argc, char *argv[]) { return Catch::Session().run(argc, argv); }
