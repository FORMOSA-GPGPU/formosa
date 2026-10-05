/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <tlm.h>

#include <cstdint>

namespace lv {

struct IpExtension : tlm::tlm_extension<IpExtension> {
  uint64_t ip = 0;

  tlm_extension_base *clone() const override {
    // Originals can be owned by an initiator or embedded in a request. Clones
    // are heap-owned by the receiving payload and must retire through free().
    struct OwnedClone : IpExtension {
      void free() override { delete this; }
    };
    IpExtension *ext = new OwnedClone;
    ext->ip = ip;
    return ext;
  }

  void copy_from(const tlm_extension_base &ext) override {
    ip = static_cast<const IpExtension &>(ext).ip;
  }

  void free() override {}
};

}  // namespace lv
