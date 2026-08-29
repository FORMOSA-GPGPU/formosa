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

class NullMM : public tlm::tlm_mm_interface {
 public:
  void free(tlm::tlm_generic_payload *payload) override { delete payload; }
} inline null_mm;

}  // namespace detail

constexpr tlm::tlm_mm_interface *Null = &detail::null_mm;

}  // namespace mm
}  // namespace lv
