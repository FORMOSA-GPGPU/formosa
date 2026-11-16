/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <systemc.h>
#include <tlm.h>

#include <cassert>
#include <condition_variable>
#include <mutex>
#include <queue>

namespace lv {

/**
 * sc_event that threads outside the SystemC kernel may notify.
 */
class ThreadSafeEvent : public sc_prim_channel {
 public:
  void notify(sc_time delay = SC_ZERO_TIME) {
    delay_ = delay;
    async_request_update();
  }
  operator const sc_event &() const { return event_; }

 protected:
  void update() override { event_.notify(delay_); }

 private:
  sc_event event_;
  sc_time delay_;
};

/**
 * Payload queue shared between SystemC processes and external threads.
 * push() may wake SystemC through push_event(); wait_pop() blocks the calling
 * OS thread, which freezes the kernel when called from a SystemC process.
 */
class ThreadSafePayloadQueue {
 public:
  using T = tlm::tlm_generic_payload *;

  void push(T payload, bool notify_simulation = true) {
    std::lock_guard<std::mutex> lock(m_);
    q_.push(payload);
    if (notify_simulation) push_event_.notify();
    cv_.notify_one();
  }

  T pop() {
    std::lock_guard<std::mutex> lock(m_);
    assert(!q_.empty());
    T tmp = q_.front();
    q_.pop();
    return tmp;
  }

  T wait_pop() {
    std::unique_lock<std::mutex> lock(m_);
    cv_.wait(lock, [&] {
      return !q_.empty();
    });
    T result = q_.front();
    q_.pop();
    return result;
  }

  size_t size() const {
    std::lock_guard<std::mutex> lock(m_);
    return q_.size();
  }

  const sc_event &push_event() const { return push_event_; }

 private:
  ThreadSafeEvent push_event_;
  mutable std::mutex m_;
  std::condition_variable cv_;
  std::queue<T> q_;
};

}  // namespace lv
