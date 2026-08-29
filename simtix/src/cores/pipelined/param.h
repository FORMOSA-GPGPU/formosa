/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/schema.h>

#include "cores/pipelined/ghost_param.h"

namespace simtix::pipelined {

class PipelinedCore;

struct Param {
  uint32_t fetch_width = 2;
  uint32_t decode_width = 2;
  uint32_t num_subcores = 2;

  // Frontend
  uint32_t num_fetch_filter_entries = 8;
  uint32_t num_fetch_entries = 8;
  bool enable_iwis = true;

  // Ghost scheduler
  bool enable_ghost_scheduler = false;
  GhostParam ghost_param;

  bool pftrace = false;
  // Emit heartbeat every N retired warp instructions; 0 disables it.
  uint64_t heartbeat_frequency = 0;
  std::optional<std::string_view> konata_trace_out = std::nullopt;

  // Backend function-unit delay queues.
  uint64_t alu_latency = 4;
  uint64_t alu_ticks_per_output = 1;
  uint64_t mdu_latency = 8;
  uint64_t mdu_ticks_per_output = 1;
  uint64_t fpu_latency = 15;
  uint64_t fpu_ticks_per_output = 1;

  // clang-format off
  LV_SCHEMA(PipelinedCore, Param,
      LV_FIELD(fetch_width, "Number of fetch requests per cycle"),
      LV_FIELD(decode_width, "Number of decoders"),
      LV_FIELD(num_subcores, "Number of subcores or warp schedulers"),
      LV_FIELD(num_fetch_filter_entries, "Number of fetch filter entries"),
      LV_FIELD(num_fetch_entries, "Number of fetch entries"),
      LV_FIELD(enable_iwis, "Whether or not to enable IWIS"),
      LV_FIELD(enable_ghost_scheduler,
               "Whether or not to enable out-of-order ghost scheduler"),
      LV_FIELD(ghost_param, "GhOST scheduler parameters"),
      LV_FIELD(pftrace, "Whether to enable Perfetto trace or not"),
      LV_FIELD(heartbeat_frequency,
               "Heartbeat interval in retired warp instructions. Set to 0 to "
               "disable."),
      LV_FIELD(konata_trace_out, "The path to the Konata trace output file"),
      LV_FIELD(alu_latency, "ALU delay queue latency in cycles"),
      LV_FIELD(alu_ticks_per_output,
               "Minimum cycle gap between ALU delay queue input/output"),
      LV_FIELD(mdu_latency, "MDU delay queue latency in cycles"),
      LV_FIELD(mdu_ticks_per_output,
               "Minimum cycle gap between MDU delay queue input/output"),
      LV_FIELD(fpu_latency, "FPU delay queue latency in cycles"),
      LV_FIELD(fpu_ticks_per_output,
               "Minimum cycle gap between FPU delay queue input/output"))
  // clang-format on
};

}  // namespace simtix::pipelined
