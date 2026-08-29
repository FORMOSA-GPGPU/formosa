/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstddef>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

namespace simtix {

template <typename T, typename... CtorArgs>
class ObjectPool {
 private:
  constexpr static size_t kDefaultPoolSize = 10;

 public:
  explicit ObjectPool(size_t init_size = kDefaultPoolSize, CtorArgs... args)
      : ctor_args_(std::move(args)...) {
    pool_.reserve(init_size);
    free_list_.reserve(init_size);

    for (size_t i = 0; i < init_size; ++i) {
      AddNewObject();
    }
  }

  T* Acquire() {
    if (free_list_.empty()) {
      AddNewObject();
    }

    T* obj = free_list_.back();
    free_list_.pop_back();
    return obj;
  }

  void Release(T* obj) { free_list_.push_back(obj); }

 private:
  template <std::size_t... Indices>
  std::unique_ptr<T> CreateObject(std::index_sequence<Indices...>) {
    return std::unique_ptr<T>(new T(std::get<Indices>(ctor_args_)...));
  }

  void AddNewObject() {
    auto obj = CreateObject(std::make_index_sequence<sizeof...(CtorArgs)>());
    free_list_.push_back(obj.get());
    pool_.push_back(std::move(obj));
  }

  std::tuple<CtorArgs...>
      ctor_args_;  // Store constructor arguments for creating new objects
  std::vector<T*>
      free_list_;  // Raw pointers for free objects, managed by pool_
  std::vector<std::unique_ptr<T>>
      pool_;  // Owns all objects to ensure proper cleanup
};

}  // namespace simtix
