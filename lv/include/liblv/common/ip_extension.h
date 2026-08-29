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
    IpExtension *ext = new IpExtension;
    ext->ip = ip;
    return ext;
  }

  void copy_from(const tlm_extension_base &ext) override {
    ip = static_cast<const IpExtension &>(ext).ip;
  }

  void free() override {}
};

}  // namespace lv
