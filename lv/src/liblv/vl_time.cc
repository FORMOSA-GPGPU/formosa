// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

// Separate TU: verilated headers already define an inline copy.
#include <systemc.h>

#include <cstdint>

uint64_t vl_time_stamp64() { return sc_core::sc_time_stamp().value(); }
