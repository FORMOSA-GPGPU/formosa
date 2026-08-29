/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/common/tlm_sink.h>
#include <liblv/schema.h>
#include <liblv/statistics.h>
#include <liblv/trace.h>
#include <systemc.h>
#include <tlm.h>

#include <cstdint>
#include <memory>
#include <sol/sol.hpp>
#include <vector>

namespace simtix {

class AtomicMemory : public sc_module {
 public:
  SC_HAS_PROCESS(AtomicMemory);

  struct Param {
    int size = 1024;
    unsigned latency = 1;
    unsigned fifo_size = 1;
    bool pftrace = false;
    // clang-format off
    LV_SCHEMA(AtomicMemory, Param,
              LV_FIELD(size, "Size of the memory in bytes"),
              LV_FIELD(latency, "Latency number of clock periods"),
              LV_FIELD(fifo_size, "Size of the output FIFO"),
              LV_FIELD(pftrace, "Enable Perfetto trace"))
    // clang-format on
  };

  AtomicMemory(const sc_module_name &name, const Param &param);
  ~AtomicMemory() override;

  using LuaBytes = sol::as_table_t<std::vector<uint8_t>>;

  LuaBytes read_bytes(uint64_t addr, size_t size) const;
  void write_bytes(uint64_t addr, const LuaBytes &data);

  void set_clock(std::shared_ptr<sc_clock> clock);
  sc_clock *clock() const { return clock_.get(); }

  auto port() { return &sink_.port; }
  size_t size() const { return size_; }
  lv::stats::Group *stats() const { return &stats_; }

 private:
  bool HandleAtomicRequest(tlm::tlm_generic_payload *trans, unsigned int len);
  void ProcessThread();
  unsigned int ProcessRequest(tlm::tlm_generic_payload *trans);

  struct Stats : lv::stats::Group {
    Metric total_reads;
    Metric total_writes;
    Formula<Integer> total_requests;
    Metric fifo_full_events;

    explicit Stats(const char *name)
        : Group(name),
          LV_STAT(total_reads, "Total number of read requests"),
          LV_STAT(total_writes, "Total number of write requests"),
          LV_STAT(total_requests, "Total number of requests"),
          LV_STAT(fifo_full_events, "Number of FIFO full events") {
      total_requests = total_reads + total_writes;
    }
  } mutable stats_;

  lv::trace::Track read_track_;
  lv::trace::Track write_track_;

  size_t size_;
  uint8_t *mem_;
  std::shared_ptr<sc_clock> clock_;
  sc_in<bool> clock_i_;
  unsigned latency_;

  lv::TlmSink sink_;
};

}  // namespace simtix
