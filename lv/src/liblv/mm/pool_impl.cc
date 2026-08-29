// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "pool_impl.h"

#include <vector>

#include "tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h"

namespace lv {
namespace mm {

tlm::tlm_generic_payload *PoolImpl::Allocate() {
  tlm::tlm_generic_payload *t = nullptr;
  if (pool_.empty()) {
    t = new tlm::tlm_generic_payload(this);
  } else {
    t = pool_.back();
    pool_.pop_back();
  }
  return t;
}

void PoolImpl::free(tlm::tlm_generic_payload *payload) {
  assert(payload);
  if (payload->get_ref_count() == 0) {
    payload->reset();
    pool_.emplace_back(payload);
  }
}

PoolImpl::PoolImpl(size_t size) {
  for (size_t i = 0; i < size; ++i) {
    pool_.emplace_back(new tlm::tlm_generic_payload(this));
  }
}

PoolImpl::~PoolImpl() {
  for (size_t i = 0; i < pool_.size(); ++i) {
    delete pool_[i];
  }
  pool_.clear();
}

}  // namespace mm
}  // namespace lv
