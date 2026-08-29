/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/common/tlm_sink.h>
#include <liblv/trace.h>
#include <tlm.h>
#include <tlm_utils/simple_target_socket.h>

#include <sol/sol.hpp>
#include <string>
#include <systemc>
#include <utility>
#include <vector>

namespace simple {
struct PfTrack {
  enum Type { PR_SLICE = 0, PR_INSTANT, PR_COUNTER, PR_CSR_END } pf_type;
  std::string track_name;
  std::vector<std::string> content_name;
};

class PerfettoReader : public sc_module {
 public:
  using Source = const tlm_utils::simple_target_socket<lv::TlmSink>;
  using TrackEntry = std::pair<PfTrack, lv::trace::Track>;
  PerfettoReader(const sc_core::sc_module_name &name,
                 const std::vector<PfTrack> &tracks);

  Source *slave_port() const;

 private:
  lv::TlmSink slave_;

  void monitor_proc();

  std::vector<TrackEntry> tracks_;
};
}  // namespace simple
