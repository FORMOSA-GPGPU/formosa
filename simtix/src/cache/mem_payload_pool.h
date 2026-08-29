/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cassert>
#include <cstddef>

#include "cache/mem_payload.h"
#include "utils/object_pool.h"

namespace simtix::cache {

class MemPayloadPool {
 public:
  explicit MemPayloadPool(size_t payload_size) : payload_size_(payload_size) {
    assert(payload_size_ > 0);
  }

  /**
   * @brief Acquire reset cache-owned payload storage.
   *
   * @return Reset payload object owned by this pool.
   */
  MemPayload *Acquire() {
    MemPayload *payload = pool_.Acquire();
    if (payload->buffer.empty() && payload->byte_enable.empty()) {
      payload->InitStorage(payload_size_);
    } else {
      assert(payload->buffer.size() == payload_size_);
      assert(payload->byte_enable.size() == payload_size_);
      payload->Reset();
    }
    return payload;
  }

  /**
   * @brief Release a cache-owned payload object back to the pool.
   *
   * @param payload Payload previously acquired from this pool.
   */
  void Release(MemPayload *payload) {
    assert(payload != nullptr);
    assert(payload->buffer.size() == payload_size_);
    assert(payload->byte_enable.size() == payload_size_);
    payload->Reset();
    pool_.Release(payload);
  }

 private:
  const size_t payload_size_;
  simtix::ObjectPool<MemPayload> pool_;
};

}  // namespace simtix::cache
