// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/mm/pool.h>

#include "pool_impl.h"
#include "tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h"

#define INITIAL_POOL_SIZE 10

namespace lv {
namespace mm {

class Pool::Impl : public PoolImpl {
 public:
  Impl() : PoolImpl(INITIAL_POOL_SIZE) {}
};

Pool::Pool() : impl_(std::make_unique<Impl>()) {}

tlm::tlm_generic_payload *Pool::Allocate() {
  return GetInstance().impl_->Allocate();
}

Pool &Pool::GetInstance() {
  static Pool mm;
  return mm;
}

}  // namespace mm
}  // namespace lv
