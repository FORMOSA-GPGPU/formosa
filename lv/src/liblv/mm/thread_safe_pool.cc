// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/mm/thread_safe_pool.h>

#include <mutex>

#include "pool_impl.h"
#include "tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h"

#define INITIAL_POOL_SIZE 10

namespace lv {
namespace mm {

class ThreadSafePool::Impl : public PoolImpl {
 public:
  Impl() : PoolImpl(INITIAL_POOL_SIZE) {}

  void free(tlm::tlm_generic_payload *payload) override {
    std::lock_guard<std::mutex> lock(m_);
    PoolImpl::free(payload);
  }

  tlm::tlm_generic_payload *Allocate() override {
    std::lock_guard<std::mutex> lock(m_);
    return PoolImpl::Allocate();
  }

 private:
  std::mutex m_;
};

ThreadSafePool::ThreadSafePool() : impl_(std::make_unique<Impl>()) {}

tlm::tlm_generic_payload *ThreadSafePool::Allocate() {
  return GetInstance().impl_->Allocate();
}

ThreadSafePool &ThreadSafePool::GetInstance() {
  static ThreadSafePool mm;
  return mm;
}

}  // namespace mm
}  // namespace lv
