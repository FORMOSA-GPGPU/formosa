/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <tlm.h>

#include <string>
#include <utility>

namespace lv {

/**
 * @brief Text an initiator attaches to a transaction for observers to read.
 *
 * An annotation is for observation only. It is an ignorable extension: no
 * module may change its behavior, timing or state because of one, and a
 * module that does not know it passes it on untouched. Observers such as
 * dbg.AccessWatcher report it with the transaction.
 *
 * An annotation travels with its payload object only. A module that issues
 * transactions of its own, such as a cache line fill or a DMA transfer, does
 * not copy it unless that module chooses to. A transaction carries at most one
 * annotation; attaching another replaces it. The text has no format that the
 * shared layers define; its producer and its reader agree on what it means.
 *
 * Attach one with set_auto_extension() on a payload that has a memory
 * manager, which frees it when the payload is reset.
 */
struct Annotation : tlm::tlm_extension<Annotation> {
  std::string text;

  explicit Annotation(std::string text = {}) : text(std::move(text)) {}

  tlm_extension_base *clone() const override { return new Annotation(text); }

  void copy_from(const tlm_extension_base &ext) override {
    text = static_cast<const Annotation &>(ext).text;
  }
};

}  // namespace lv
