// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/axi/axi_initiator_adapter.h>

namespace lv::axi::detail {

void AxiResponseOrder::Register(uint32_t txn_id, Payload *payload) {
  pending_[txn_id].push_back(payload);
}

void AxiResponseOrder::Complete(Payload *payload) {
  completed_.insert(payload);
}

AxiResponseOrder::Payload *AxiResponseOrder::TakeReady() {
  for (auto pending_it = pending_.begin(); pending_it != pending_.end();
       ++pending_it) {
    auto &queue = pending_it->second;
    if (queue.empty()) continue;
    auto completed_it = completed_.find(queue.front());
    if (completed_it == completed_.end()) continue;

    auto *payload = *completed_it;
    completed_.erase(completed_it);
    queue.pop_front();
    if (queue.empty()) pending_.erase(pending_it);
    return payload;
  }
  return nullptr;
}

}  // namespace lv::axi::detail
