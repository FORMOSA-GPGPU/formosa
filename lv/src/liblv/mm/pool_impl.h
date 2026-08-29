/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/mm/pool.h>
#include <liblv/mm/thread_safe_pool.h>

#include <vector>

namespace lv {
namespace mm {

class PoolImpl : tlm::tlm_mm_interface {
 private:
  explicit PoolImpl(size_t size);
  virtual ~PoolImpl();
  void free(tlm::tlm_generic_payload *payload) override;
  virtual tlm::tlm_generic_payload *Allocate();

  std::vector<tlm::tlm_generic_payload *> pool_;

  friend class Pool;
  friend class ThreadSafePool;
};

}  // namespace mm

}  // namespace lv
