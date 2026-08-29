/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h"

namespace lv {
namespace mm {

namespace detail {

/**
 * @brief StaticMM is a memory manager implementation that performs no action on
 * free().
 *
 * This is specifically designed for statically allocated tlm_generic_payload
 * objects, or payloads whose lifetime is managed externally. By setting a
 * payload's MM to StaticMM, you can safely use the acquire() and release()
 * reference counting mechanism without the risk of the payload being deleted by
 * the MM.
 */
class StaticMM : public tlm::tlm_mm_interface {
 public:
  virtual ~StaticMM() = default;

  /**
   * @brief Does nothing. Prevents the payload from being deallocated.
   */
  void free(tlm::tlm_generic_payload * /*payload*/) override {
    // No-op: The payload's lifetime is managed by the owner of the MSHR.
  }
};

// Internal instance to be used globally within the namespace.
inline StaticMM static_mm_instance;

}  // namespace detail

/**
 * @brief Constant pointer to the global StaticMM instance.
 * Usage: payload.set_mm(lv::mm::Static);
 */
constexpr tlm::tlm_mm_interface *Static = &detail::static_mm_instance;

}  // namespace mm
}  // namespace lv
