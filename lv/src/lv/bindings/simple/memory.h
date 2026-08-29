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
#include <sysc/kernel/sc_event.h>
#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/simple_target_socket.h>

#include <elfio/elfio.hpp>
#include <sol/sol.hpp>

namespace simple {

class Memory : public sc_module {
 public:
  struct Param {
    int size = 1024;
    unsigned latency = 1;
    unsigned fifo_size = 1;
    bool trace = false;
    // clang-format off
    LV_SCHEMA(Memory, Param,
              LV_FIELD(size, "Size of the memory in bytes"),
              LV_FIELD(latency, "Latency number of clock periods"),
              LV_FIELD(fifo_size, "Size of the output FIFO"),
              LV_FIELD(trace, "Enable Perfetto trace"))
    // clang-format on
  };
  Memory(const sc_module_name &name, const Param &param);
  ~Memory();

  // Lua API implementations
  using LuaBytes = sol::as_table_t<std::vector<uint8_t>>;

  LuaBytes read_bytes(uint64_t addr, size_t size) const;
  void write_bytes(uint64_t addr, const LuaBytes &data);

  void set_clock(sc_clock *clock);
  sc_clock *clock() const;

  auto port() const { return &sink_.port; }

  size_t size() const { return size_; }

  void load_elf(ELFIO::elfio &elf);

  lv::stats::Group *stats() const { return &stats_; }

 private:
  lv::trace::Track read_track_;
  lv::trace::Track write_track_;

  size_t size_;
  uint8_t *mem_;
  sc_clock *clock_;
  sc_in<bool> clock_i_;
  unsigned latency_;

  lv::TlmSink sink_;

  void ProcessThread();

  unsigned int ProcessRequest(tlm::tlm_generic_payload *trans);

  struct Stats : lv::stats::Group {
    Metric total_reads;
    Metric total_writes;
    Formula<Integer> total_requests;
    Metric fifo_full_events;

    Stats(const char *name)
        : Group(name),
          LV_STAT(total_reads, "Total number of read requests"),
          LV_STAT(total_writes, "Total number of write requests"),
          LV_STAT(total_requests, "Total number of requests"),
          LV_STAT(fifo_full_events, "Number of FIFO full events") {
      total_requests = total_reads + total_writes;
    }
  } mutable stats_;
};

}  // namespace simple
