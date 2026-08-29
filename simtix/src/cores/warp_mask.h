/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <systemc.h>

namespace simtix {

class WarpMask {
 public:
  WarpMask(bool init_val, int width) : val_(init_val, width) {}

  void operator=(const sc_bv_base &val) {
    if (val_ != val) {
      val_ = val;
      value_changed_event_.notify(SC_ZERO_TIME);
    }
  }

  const sc_bv_base &val() const { return val_; }
  operator const sc_bv_base &() const { return val(); }
  const sc_event &value_changed_event() const { return value_changed_event_; }

 private:
  sc_bv_base val_;
  sc_event value_changed_event_;
};

}  // namespace simtix
