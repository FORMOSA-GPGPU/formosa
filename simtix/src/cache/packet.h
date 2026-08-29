/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/output.h>
#include <tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "cache/mem_payload.h"

namespace simtix::cache {

struct Location {
  size_t set = 0;
  size_t way = 0;
};

enum class PacketType {
  kUnused = 0,
  kBypassCoreReq,
  kBypassCoreResp,
  kCoreReq,
  kRefill,
  kReplay,
  kMshrReadReq,
  kVictimWriteReq,
  kVictimWriteResp,
  kMemWriteReq,
  kMemWriteResp,
};

/** @brief Generation-protected identity carried by an MSHR refill packet. */
struct MshrId {
  size_t index = 0;
  uint64_t generation = 0;

  bool operator==(const MshrId &other) const {
    return index == other.index && generation == other.generation;
  }
};

enum class PayloadType {
  kNone = 0,
  kCorePayload,
  kCacheOwnedPayload,
};

class Packet {
 public:
  size_t unique_id;

  // For debug and trace purposes, record the parent packet that derived this
  // packet.
  std::optional<size_t> trace_parent_id = std::nullopt;

  PacketType type = PacketType::kUnused;
  bool is_atomic = false;

  // cache lookup metadata
  bool is_hit = false;
  // whether this cache refill will trigger a victim eviction and the victim
  // cache line is dirty
  bool is_victim_dirty = false;
  uint64_t victim_address = 0;
  Location location;
  // Correlates a cache-owned read/refill packet with its logical MSHR entry.
  std::optional<MshrId> mshr_id = std::nullopt;

  /**
   * @brief Return which payload storage currently backs this packet.
   *
   * @return Current payload ownership/type state.
   */
  PayloadType payload_type() const { return payload_type_; }

  /**
   * @brief Check whether the packet's TLM payload is a write transaction.
   *
   * @return true when the backing TLM payload is a write command.
   */
  bool is_write() const {
    switch (payload_type_) {
      case PayloadType::kCorePayload:
        assert(core_payload_ != nullptr);
        if (core_payload_ == nullptr) {
          lv::Fatal("Core payload packet has null payload\n");
        }
        return core_payload_->is_write();
      case PayloadType::kCacheOwnedPayload:
        assert(cache_owned_payload_ != nullptr);
        if (cache_owned_payload_ == nullptr) {
          lv::Fatal("Cache-owned payload packet has null payload\n");
        }
        return cache_owned_payload_->gp.is_write();
      case PayloadType::kNone:
        break;
    }

    assert(false && "Packet has no payload");
    lv::Fatal("Packet has no payload\n");
    return false;
  }

  /**
   * @brief Return the TLM generic payload carried by this packet.
   *
   * @return Pointer to the active TLM generic payload.
   */
  tlm::tlm_generic_payload *GetTlmGp() const {
    switch (payload_type_) {
      case PayloadType::kCorePayload:
        assert(core_payload_ != nullptr);
        if (core_payload_ == nullptr) {
          lv::Fatal("Core payload packet has null payload\n");
        }
        return core_payload_;
      case PayloadType::kCacheOwnedPayload:
        assert(cache_owned_payload_ != nullptr);
        if (cache_owned_payload_ == nullptr) {
          lv::Fatal("Cache-owned payload packet has null payload\n");
        }
        return &cache_owned_payload_->gp;
      case PayloadType::kNone:
        break;
    }

    assert(false && "Packet has no payload");
    lv::Fatal("Packet has no payload\n");
    return nullptr;
  }

  /**
   * @brief Return the byte address from the active TLM payload.
   *
   * @return Address carried by `GetTlmGp()`.
   */
  uint64_t GetAddress() const { return GetTlmGp()->get_address(); }

  /**
   * @brief Attach a borrowed core-owned TLM payload.
   *
   * @param payload Borrowed core-side payload.
   */
  void SetPayload(tlm::tlm_generic_payload *payload) {
    assert(payload != nullptr);
    payload_type_ = PayloadType::kCorePayload;
    core_payload_ = payload;
    cache_owned_payload_ = nullptr;
  }

  /**
   * @brief Attach a cache-owned memory payload.
   *
   * @param mem_payload Cache-owned payload storage.
   */
  void SetPayload(MemPayload *mem_payload) {
    assert(mem_payload != nullptr);
    payload_type_ = PayloadType::kCacheOwnedPayload;
    core_payload_ = nullptr;
    cache_owned_payload_ = mem_payload;
  }

  /**
   * @brief Return the cache-owned payload attached to this packet.
   *
   * @return Pointer to the active cache-owned payload.
   */
  MemPayload *GetCacheOwnedPayload() const {
    assert(payload_type_ == PayloadType::kCacheOwnedPayload);
    if (payload_type_ != PayloadType::kCacheOwnedPayload) {
      lv::Fatal("Packet does not own a cache payload\n");
    }
    assert(cache_owned_payload_ != nullptr);
    if (cache_owned_payload_ == nullptr) {
      lv::Fatal("Cache-owned payload packet has null payload\n");
    }
    return cache_owned_payload_;
  }

  /**
   * @brief Clear transient packet state before returning it to a pool.
   */
  void Reset() {
    unique_id = 0;
    trace_parent_id = std::nullopt;
    type = PacketType::kUnused;
    is_atomic = false;
    payload_type_ = PayloadType::kNone;
    core_payload_ = nullptr;
    cache_owned_payload_ = nullptr;
    is_hit = false;
    location = {};
    is_victim_dirty = false;
    victim_address = 0;
    mshr_id = std::nullopt;
  }

 private:
  PayloadType payload_type_ = PayloadType::kNone;
  tlm::tlm_generic_payload *core_payload_ = nullptr;
  MemPayload *cache_owned_payload_ = nullptr;
};

/**
 * @brief Record packet derivation for debug and trace consumers.
 *
 * @param child Derived packet.
 * @param parent Source packet that caused the derived transaction.
 */
inline void SetTraceParent(Packet *child, const Packet *parent) {
  assert(child != nullptr);
  assert(parent != nullptr);
  child->trace_parent_id = parent->unique_id;
}

}  // namespace simtix::cache
