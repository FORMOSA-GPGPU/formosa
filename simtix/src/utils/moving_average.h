/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <vector>

namespace simtix {

template <class T>
class MovingAverage {
 public:
  explicit MovingAverage(uint32_t window_size = 128)
      : window_size_(window_size), buf_(window_size, 0) {}

  MovingAverage &operator<<(T data) {
    Advance(data);
    return *this;
  }

  void Advance(T data) {
    sum_ -= buf_[head_];
    buf_[head_] = data;
    sum_ += data;
    head_ = (head_ + 1) % window_size_;
  }

  double get() const { return static_cast<double>(sum_) / window_size_; }

 private:
  const uint32_t window_size_;
  std::vector<T> buf_;
  uint32_t head_ = 0;
  T sum_ = 0;
};

}  // namespace simtix
