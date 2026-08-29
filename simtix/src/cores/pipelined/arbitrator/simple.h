/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "cores/pipelined/arbitrator/base.h"

namespace simtix::pipelined {

class SimpleArbitrator : public ArbitratorIntf {
 public:
  SimpleArbitrator(const sc_module_name &name, const ArchParam &param)
      : ArbitratorIntf(name), num_lanes_(param.num_lanes) {
    SC_METHOD(HandleOperandCollectReq);
    SC_METHOD(HandleWritebackReq);
  }

 private:
  const uint32_t num_lanes_;
  void HandleOperandCollectReq();
  void HandleWritebackReq();
};

}  // namespace simtix::pipelined
