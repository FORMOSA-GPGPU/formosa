/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <systemc.h>

#include <cstdint>

namespace simtix {

struct ExecContext {
  // Read only fields, the context.
  const uint64_t pc;
  const sc_bv_base &tmask;
  const int64_t *rs1_data;
  const int64_t *rs2_data;
  const int64_t *rs3_data;
  const uint8_t *dynamic_rm;  // Per-lane dynamic rounding mode
  const std::size_t num_lanes;

  // Result sinks, the execution result.
  int64_t *rd_data;   // Per-lane RD data
  uint64_t *next_pc;  // Per-lane next PC

  struct {
    uint64_t *addr;  // Per-lane addr
    int64_t *data;   // Per-lane data
  } mem;

  struct {
    uint32_t *addr;  // Per-instruction CSR address
    int64_t *data;   // Per-lane data
  } csr;

  uint8_t *pri;  // Per-instruction priority

  uint8_t *fflags;  // Per-lane fflags sink
};

}  // namespace simtix
