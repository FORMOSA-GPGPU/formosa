// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cache/tag_array.h"

#include <cstddef>
#include <cstdint>

#include "cache/packet.h"
#include "cache/policies.h"
#include "catch2/catch_session.hpp"
#include "catch2/catch_test_macros.hpp"
#include "catch2/generators/catch_generators.hpp"
#include "catch2/generators/catch_generators_range.hpp"

namespace {

using simtix::cache::Packet;
using simtix::cache::PacketType;
using simtix::cache::Param;
using simtix::cache::ReplacementPolicy;
using simtix::cache::TagArray;
using simtix::cache::WriteHitPolicy;

Param MakeParam(size_t cache_size, size_t block_size, size_t ways,
                ReplacementPolicy replacement_policy,
                WriteHitPolicy write_hit_policy, uint64_t random_seed = 0) {
  Param param;
  param.cache_size_bytes = cache_size;
  param.block_size_bytes = block_size;
  param.ways = ways;
  param.replacement_policy = replacement_policy;
  param.write_hit_policy = write_hit_policy;
  param.random_seed = random_seed;
  return param;
}

class PacketBuilder {
 public:
  PacketBuilder(PacketType type, uint64_t address, bool is_write = false)
      : payload_(std::make_unique<tlm::tlm_generic_payload>()) {
    tlm::tlm_command command =
        is_write ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND;

    payload_->set_address(address);
    payload_->set_command(command);
    packet_.type = type;
    packet_.SetPayload(payload_.get());
  }

  Packet *packet() { return &packet_; }

  /**
   * Packet builder has a unique_ptr to manage the payload memory, so it cannot
   * be copied
   */
  PacketBuilder(const PacketBuilder &) = delete;
  PacketBuilder &operator=(const PacketBuilder &) = delete;
  PacketBuilder(PacketBuilder &&) = default;
  PacketBuilder &operator=(PacketBuilder &&) = default;

 private:
  std::unique_ptr<tlm::tlm_generic_payload> payload_;
  Packet packet_{};
};

void TimeAdvance() { sc_core::sc_start(sc_core::sc_time(1, sc_core::SC_NS)); }

}  // namespace

namespace simtix::cache {

class TagArrayTester {
 public:
  explicit TagArrayTester(TagArray &dut) : dut_(dut) {}

  PacketBuilder CoreReq(uint64_t addr, bool is_write = false) {
    PacketBuilder p(PacketType::kCoreReq, addr, is_write);
    dut_.Process(p.packet());
    TimeAdvance();
    return p;
  }

  PacketBuilder Refill(uint64_t addr) {
    PacketBuilder p(PacketType::kRefill, addr);
    dut_.Process(p.packet());
    TimeAdvance();
    return p;
  }

  PacketBuilder Replay(uint64_t addr, bool is_write = false) {
    PacketBuilder p(PacketType::kReplay, addr, is_write);
    dut_.Process(p.packet());
    TimeAdvance();
    return p;
  }

  PacketBuilder Probe(PacketType type, uint64_t addr, bool is_write = false) {
    REQUIRE((type == PacketType::kCoreReq || type == PacketType::kReplay));

    PacketBuilder p(type, addr, is_write);
    dut_.Process(p.packet());
    TimeAdvance();
    return p;
  }

  size_t ToSetIndex(uint64_t addr) { return dut_.ToSetIndex(addr); }

  uint64_t ToLineAddress(uint64_t addr) { return dut_.ToLineAddress(addr); }

  bool IsDirty(uint64_t addr) const {
    auto lookup = dut_.FindTagEntry(addr);
    REQUIRE(lookup.has_value());
    return lookup->entry->dirty;
  }

  size_t WayOf(uint64_t addr) {
    auto lookup = dut_.FindTagEntry(addr);
    REQUIRE(lookup.has_value());
    return lookup->way;
  }

 private:
  TagArray &dut_;
};

}  // namespace simtix::cache

using simtix::cache::TagArrayTester;

namespace {

struct ProbeArgs {
  PacketType packet_type;
  bool is_write;
};

inline auto all_probe_combinations() {
  return std::vector<ProbeArgs>{{PacketType::kCoreReq, true},
                                {PacketType::kCoreReq, false},
                                {PacketType::kReplay, true},
                                {PacketType::kReplay, false}};
}

}  // namespace

SCENARIO("Tag array probe", "[cache][tag_array]") {
  GIVEN("an empty two-way tag array") {
    TagArray tag_array(MakeParam(128, 16, 2, ReplacementPolicy::kFIFO,
                                 WriteHitPolicy::kWriteBack));
    TagArrayTester driver(tag_array);
    const uint64_t address = 0x30;

    WHEN("a core request probes an unfilled line") {
      auto probe = driver.CoreReq(address);

      THEN("the packet is marked as miss") {
        CHECK_FALSE(probe.packet()->is_hit);
      }
    }

    WHEN("line A is refilled and probed") {
      auto refill = driver.Refill(address);

      REQUIRE(refill.packet()->is_hit);
      REQUIRE(refill.packet()->location.set == driver.ToSetIndex(address));
      REQUIRE(refill.packet()->location.way == 0);

      auto args = GENERATE(from_range(all_probe_combinations()));
      CAPTURE(args.packet_type, args.is_write);

      auto probe = driver.Probe(args.packet_type, address, args.is_write);

      THEN("the packet is marked as a hit") { CHECK(probe.packet()->is_hit); }
      AND_THEN("the packet location points to the correct cache line") {
        CHECK(probe.packet()->location.set == driver.ToSetIndex(address));
        CHECK(probe.packet()->location.way == driver.WayOf(address));
      }

      WHEN("same line address is probed") {
        const uint64_t addr_same_line = address + 1;
        REQUIRE(driver.ToLineAddress(addr_same_line) ==
                driver.ToLineAddress(address));

        auto args = GENERATE(from_range(all_probe_combinations()));
        CAPTURE(args.packet_type, args.is_write);
        auto probe_same_line =
            driver.Probe(args.packet_type, addr_same_line, args.is_write);

        THEN("the packet is marked as a hit") {
          CHECK(probe_same_line.packet()->is_hit);
        }
        AND_THEN("the packet location points to the correct cache line") {
          CHECK(probe_same_line.packet()->location.set ==
                driver.ToSetIndex(addr_same_line));
          CHECK(probe_same_line.packet()->location.way ==
                driver.WayOf(address));
        }
      }

      WHEN("a different line mapping to the same set is probed") {
        const uint64_t addr_same_set = address + 0x40;
        REQUIRE(driver.ToSetIndex(addr_same_set) == driver.ToSetIndex(address));
        REQUIRE(driver.ToLineAddress(addr_same_set) !=
                driver.ToLineAddress(address));

        bool is_write = GENERATE(true, false);
        CAPTURE(is_write);
        auto core_req_same_set = driver.CoreReq(addr_same_set, is_write);

        THEN("the packet is marked as a miss") {
          CHECK_FALSE(core_req_same_set.packet()->is_hit);
        }
      }
    }
  }
}

SCENARIO("Tag array write-back hit marks line dirty", "[cache][tag_array]") {
  GIVEN("a WriteBack cache with line A filled") {
    TagArray tag_array(MakeParam(128, 16, 2, ReplacementPolicy::kFIFO,
                                 WriteHitPolicy::kWriteBack));
    TagArrayTester driver(tag_array);
    const uint64_t addr_a = 0x20;
    driver.Refill(addr_a);

    WHEN("line A is probed with a write request") {
      auto probe = driver.CoreReq(addr_a, true);

      THEN("the packet is marked as a hit and the line is marked dirty") {
        CHECK(probe.packet()->is_hit);
        CHECK(driver.IsDirty(addr_a));
      }

      AND_WHEN("line A is probed again") {
        auto args = GENERATE(from_range(all_probe_combinations()));
        CAPTURE(args.packet_type, args.is_write);
        auto probe_again =
            driver.Probe(args.packet_type, addr_a, args.is_write);

        THEN("the packet is marked as a hit and the line remains dirty") {
          CHECK(probe_again.packet()->is_hit);
          CHECK(driver.IsDirty(addr_a));
        }
      }
    }
  }
}

SCENARIO("Tag array write-through hit does not mark line dirty",
         "[cache][tag_array]") {
  GIVEN("a WriteThrough cache with line A filled") {
    TagArray tag_array(MakeParam(128, 16, 2, ReplacementPolicy::kFIFO,
                                 WriteHitPolicy::kWriteThrough));
    TagArrayTester driver(tag_array);
    const uint64_t addr_a = 0x20;
    driver.Refill(addr_a);

    WHEN("line A is probed again") {
      auto args = GENERATE(from_range(all_probe_combinations()));
      CAPTURE(args.packet_type, args.is_write);
      auto probe = driver.Probe(args.packet_type, addr_a, args.is_write);

      THEN("the packet is marked as a hit and the line remains clean") {
        CHECK(probe.packet()->is_hit);
        CHECK_FALSE(driver.IsDirty(addr_a));
      }
    }
  }
}

SCENARIO("Tag array refill uses invalid ways before eviction",
         "[cache][tag_array]") {
  GIVEN("an empty two-way set") {
    auto replacement_policy =
        GENERATE(ReplacementPolicy::kFIFO, ReplacementPolicy::kLRU,
                 ReplacementPolicy::kRandom);

    // Write hit policy should not affect the refill behavior, so we can just
    // use WriteBack for this test
    TagArray tag_array(
        MakeParam(128, 16, 2, replacement_policy, WriteHitPolicy::kWriteBack));
    TagArrayTester driver(tag_array);

    WHEN("two lines mapping to the same set are refilled") {
      const uint64_t addr_a = 0x20;
      const uint64_t addr_b = 0x60;
      REQUIRE(driver.ToSetIndex(addr_a) == driver.ToSetIndex(addr_b));

      auto refill_a = driver.Refill(addr_a);
      auto refill_b = driver.Refill(addr_b);

      THEN("no victim is reported and both lines can be probed as hits") {
        CHECK_FALSE(refill_a.packet()->is_victim_dirty);
        CHECK_FALSE(refill_b.packet()->is_victim_dirty);
        CHECK(refill_a.packet()->victim_address == 0);
        CHECK(refill_b.packet()->victim_address == 0);
        CHECK(refill_a.packet()->location.set == driver.ToSetIndex(addr_a));
        CHECK(refill_b.packet()->location.set == driver.ToSetIndex(addr_b));
        CHECK(refill_a.packet()->location.way == 0);
        CHECK(refill_b.packet()->location.way == 1);

        auto probe_a = driver.CoreReq(addr_a);
        auto probe_b = driver.CoreReq(addr_b);
        CHECK(probe_a.packet()->is_hit);
        CHECK(probe_b.packet()->is_hit);
        CHECK(probe_a.packet()->location.way == 0);
        CHECK(probe_b.packet()->location.way == 1);
      }

      AND_WHEN("both lines are clean") {
        AND_WHEN("a third line mapping to the same set is refilled") {
          const uint64_t addr_c = 0xA0;
          REQUIRE(driver.ToSetIndex(addr_c) == driver.ToSetIndex(addr_a));

          auto refill_c = driver.Refill(addr_c);

          THEN("the packet should report a victim") {
            CHECK_FALSE(refill_c.packet()->is_victim_dirty);
            CHECK(refill_c.packet()->location.set == driver.ToSetIndex(addr_c));
            if (replacement_policy != ReplacementPolicy::kRandom) {
              CHECK(refill_c.packet()->victim_address == addr_a);
              CHECK(refill_c.packet()->location.way == 0);
            }
          }

          AND_THEN("one previous line is evicted") {
            const bool a_hit = driver.CoreReq(addr_a).packet()->is_hit;
            const bool b_hit = driver.CoreReq(addr_b).packet()->is_hit;
            const bool c_hit = driver.CoreReq(addr_c).packet()->is_hit;

            CHECK(c_hit);
            CHECK(a_hit != b_hit);
          }
        }
      }

      AND_WHEN("both lines are dirty") {
        auto write_a = driver.Replay(addr_a, true);
        auto write_b = driver.Replay(addr_b, true);

        THEN("both lines should be hits") {
          REQUIRE(write_a.packet()->is_hit);
          REQUIRE(write_b.packet()->is_hit);
        }

        AND_WHEN("a third line mapping to the same set is refilled") {
          const uint64_t addr_c = 0xA0;
          REQUIRE(driver.ToSetIndex(addr_c) == driver.ToSetIndex(addr_a));

          auto refill_c = driver.Refill(addr_c);

          THEN("the packet should report a dirty victim") {
            CHECK(refill_c.packet()->is_victim_dirty);
            CHECK(refill_c.packet()->location.set == driver.ToSetIndex(addr_c));
            if (replacement_policy != ReplacementPolicy::kRandom) {
              CHECK(refill_c.packet()->victim_address == addr_a);
              CHECK(refill_c.packet()->location.way == 0);
            }
          }

          AND_THEN("one previous line is evicted") {
            const bool a_hit = driver.CoreReq(addr_a).packet()->is_hit;
            const bool b_hit = driver.CoreReq(addr_b).packet()->is_hit;
            const bool c_hit = driver.CoreReq(addr_c).packet()->is_hit;

            CHECK(c_hit);
            CHECK(a_hit != b_hit);
          }
        }
      }
    }
  }
}

SCENARIO("Tag array random replacement is reproducible for a fixed seed",
         "[cache][tag_array]") {
  TagArray first(MakeParam(128, 16, 2, ReplacementPolicy::kRandom,
                           WriteHitPolicy::kWriteBack, 2));
  TagArray second(MakeParam(128, 16, 2, ReplacementPolicy::kRandom,
                            WriteHitPolicy::kWriteBack, 2));
  TagArrayTester first_driver(first);
  TagArrayTester second_driver(second);

  GIVEN("two random-policy arrays with the same seed and refill stream") {
    const std::vector<uint64_t> addresses = {0x20, 0x60,  0xA0,
                                             0xE0, 0x120, 0x160};
    std::vector<size_t> first_victims;
    std::vector<size_t> second_victims;

    for (size_t index = 0; index < addresses.size(); ++index) {
      REQUIRE(first_driver.ToSetIndex(addresses.front()) ==
              first_driver.ToSetIndex(addresses[index]));
      auto first_refill = first_driver.Refill(addresses[index]);
      auto second_refill = second_driver.Refill(addresses[index]);

      if (index >= 2) {
        first_victims.push_back(first_refill.packet()->location.way);
        second_victims.push_back(second_refill.packet()->location.way);
      }
    }

    THEN("both arrays select the same legal victim sequence") {
      CHECK(first_victims == second_victims);
      for (size_t way : first_victims) {
        CHECK(way < 2);
      }
    }
  }
}

SCENARIO("Tag array LRU replacement policy evicts the least recently used line",
         "[cache][tag_array]") {
  TagArray tag_array(MakeParam(128, 16, 2, ReplacementPolicy::kLRU,
                               WriteHitPolicy::kWriteBack));
  TagArrayTester driver(tag_array);

  GIVEN("a full two-way LRU set using LRU") {
    const uint64_t addr_a = 0x20;
    const uint64_t addr_b = 0x60;

    REQUIRE(driver.ToSetIndex(addr_a) == driver.ToSetIndex(addr_b));

    driver.Refill(addr_a);
    driver.Refill(addr_b);

    WHEN("line A is accessed before refilling line C") {
      driver.CoreReq(addr_a);

      const uint64_t addr_c = 0xA0;
      REQUIRE(driver.ToSetIndex(addr_c) == driver.ToSetIndex(addr_a));

      auto refill_c = driver.Refill(addr_c);

      THEN("line B should be evicted") {
        CHECK(refill_c.packet()->victim_address == addr_b);
        CHECK(refill_c.packet()->location.way == 1);
        CHECK(driver.CoreReq(addr_a).packet()->is_hit);
        CHECK_FALSE(driver.CoreReq(addr_b).packet()->is_hit);
        CHECK(driver.CoreReq(addr_c).packet()->is_hit);
      }
    }
  }
}

SCENARIO("Tag array FIFO replacement policy evicts the first line filled",
         "[cache][tag_array]") {
  TagArray tag_array(MakeParam(128, 16, 2, ReplacementPolicy::kFIFO,
                               WriteHitPolicy::kWriteBack));
  TagArrayTester driver(tag_array);

  GIVEN("a full two-way FIFO set") {
    const uint64_t addr_a = 0x20;
    const uint64_t addr_b = 0x60;

    REQUIRE(driver.ToSetIndex(addr_a) == driver.ToSetIndex(addr_b));

    WHEN("line A is filled before line B") {
      driver.Refill(addr_a);
      driver.Refill(addr_b);

      const uint64_t addr_c = 0xA0;
      REQUIRE(driver.ToSetIndex(addr_c) == driver.ToSetIndex(addr_a));

      AND_WHEN("line C is refilled") {
        auto refill_c = driver.Refill(addr_c);

        THEN("line A should be evicted") {
          CHECK(refill_c.packet()->victim_address == addr_a);
          CHECK(refill_c.packet()->location.way == 0);
          CHECK_FALSE(driver.CoreReq(addr_a).packet()->is_hit);
          CHECK(driver.CoreReq(addr_b).packet()->is_hit);
          CHECK(driver.CoreReq(addr_c).packet()->is_hit);
        }
      }

      AND_WHEN("line A is accessed again before refilling line C") {
        REQUIRE(driver.CoreReq(addr_a).packet()->is_hit);

        auto refill_c = driver.Refill(addr_c);

        THEN("line A should still be evicted") {
          CHECK(refill_c.packet()->victim_address == addr_a);
          CHECK(refill_c.packet()->location.way == 0);
          CHECK_FALSE(driver.CoreReq(addr_a).packet()->is_hit);
          CHECK(driver.CoreReq(addr_b).packet()->is_hit);
          CHECK(driver.CoreReq(addr_c).packet()->is_hit);
        }
      }
    }
  }
}

SCENARIO("Tag array refill does not evict locked entries",
         "[cache][tag_array]") {
  GIVEN("a full two-way FIFO set with one locked way") {
    TagArray tag_array(MakeParam(128, 16, 2, ReplacementPolicy::kFIFO,
                                 WriteHitPolicy::kWriteBack));
    TagArrayTester driver(tag_array);
    const uint64_t addr_a = 0x20;
    const uint64_t addr_b = 0x60;
    const uint64_t addr_c = 0xA0;
    REQUIRE(driver.ToSetIndex(addr_a) == driver.ToSetIndex(addr_b));
    REQUIRE(driver.ToSetIndex(addr_a) == driver.ToSetIndex(addr_c));

    auto refill_a = driver.Refill(addr_a);
    driver.Refill(addr_b);
    tag_array.LockEntry(refill_a.packet()->location);

    WHEN("a third line is refilled") {
      auto refill_c = driver.Refill(addr_c);

      THEN("the unlocked way is selected even though the locked way is older") {
        CHECK(refill_c.packet()->is_hit);
        CHECK(refill_c.packet()->victim_address == addr_b);
        CHECK(refill_c.packet()->location.way == 1);
        CHECK(driver.CoreReq(addr_a).packet()->is_hit);
        CHECK_FALSE(driver.CoreReq(addr_b).packet()->is_hit);
        CHECK(driver.CoreReq(addr_c).packet()->is_hit);
      }
    }
  }

  GIVEN("a direct-mapped set whose only way is locked") {
    TagArray tag_array(MakeParam(64, 16, 1, ReplacementPolicy::kFIFO,
                                 WriteHitPolicy::kWriteBack));
    TagArrayTester driver(tag_array);
    const uint64_t addr_a = 0x00;
    const uint64_t addr_b = 0x40;
    auto refill_a = driver.Refill(addr_a);
    tag_array.LockEntry(refill_a.packet()->location);

    WHEN("a conflicting line is refilled") {
      PacketBuilder refill_b(PacketType::kRefill, addr_b);
      auto status = tag_array.Process(refill_b.packet());

      THEN("the refill stalls without mutating the locked set") {
        CHECK(status == TagArray::AccessStatus::kNoVictim);
        CHECK_FALSE(refill_b.packet()->is_hit);
        CHECK(driver.CoreReq(addr_a).packet()->is_hit);
        CHECK_FALSE(driver.CoreReq(addr_b).packet()->is_hit);
      }

      AND_WHEN("the locked way is released and the same refill retries") {
        tag_array.UnlockEntry(refill_a.packet()->location);
        status = tag_array.Process(refill_b.packet());

        THEN("the refill can evict the released way") {
          CHECK(status == TagArray::AccessStatus::kHit);
          CHECK(refill_b.packet()->is_hit);
          CHECK(refill_b.packet()->victim_address == addr_a);
          CHECK_FALSE(driver.CoreReq(addr_a).packet()->is_hit);
          CHECK(driver.CoreReq(addr_b).packet()->is_hit);
        }
      }
    }
  }
}

int sc_main(int argc, char *argv[]) { return Catch::Session().run(argc, argv); }
