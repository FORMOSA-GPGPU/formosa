/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <memory>

#include "tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h"

namespace lv {
namespace mm {

class ThreadSafePool {
 public:
  static tlm::tlm_generic_payload *Allocate();

 private:
  static ThreadSafePool &GetInstance();
  explicit ThreadSafePool();

  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace mm
}  // namespace lv
