/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <tlm.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace simtix::cache {

struct KonataExtension : tlm::tlm_extension<KonataExtension> {
  uint64_t unique_id = 0;
  uint32_t thread_id = 0;
  uint64_t parent_id = 0;
  bool declared = false;
  void *pool_owner = nullptr;

  void ResetTraceState() {
    unique_id = 0;
    thread_id = 0;
    parent_id = 0;
    declared = false;
  }

  tlm_extension_base *clone() const override {
    KonataExtension *ext = new KonataExtension;
    ext->unique_id = unique_id;
    ext->thread_id = thread_id;
    ext->parent_id = parent_id;
    ext->declared = declared;
    return ext;
  }

  void copy_from(const tlm_extension_base &ext) override {
    const auto &other = static_cast<const KonataExtension &>(ext);
    unique_id = other.unique_id;
    thread_id = other.thread_id;
    parent_id = other.parent_id;
    declared = other.declared;
  }

  void free() override {}
};

inline KonataExtension *GetKonataExtension(tlm::tlm_generic_payload *payload) {
  KonataExtension *ext = nullptr;
  payload->get_extension(ext);
  return ext;
}

inline const KonataExtension *GetKonataExtension(
    const tlm::tlm_generic_payload *payload) {
  KonataExtension *ext = nullptr;
  const_cast<tlm::tlm_generic_payload *>(payload)->get_extension(ext);
  return ext;
}

class KonataExtensionPool {
 public:
  explicit KonataExtensionPool(uint32_t initial_size) {
    pool_.reserve(initial_size);
    free_list_.reserve(initial_size);
    for (uint32_t i = 0; i < initial_size; ++i) {
      AddNewPacket();
    }
  }

  KonataExtension *Acquire() {
    if (free_list_.empty()) {
      AddNewPacket();
    }
    KonataExtension *p = free_list_.back();
    free_list_.pop_back();
    p->ResetTraceState();
    p->unique_id = unique_id_++;
    return p;
  }

  uint64_t GenerateUniqueId() { return unique_id_++; }

  void Release(KonataExtension *p) {
    p->ResetTraceState();
    free_list_.push_back(p);
  }

 private:
  void AddNewPacket() {
    auto new_packet = std::make_unique<KonataExtension>();
    free_list_.push_back(new_packet.get());
    pool_.push_back(std::move(new_packet));
  }

  std::vector<KonataExtension *> free_list_;            // Fast LIFO stack
  std::vector<std::unique_ptr<KonataExtension>> pool_;  // Permanent Owners

  inline static uint64_t unique_id_ = 1;
};

}  // namespace simtix::cache
