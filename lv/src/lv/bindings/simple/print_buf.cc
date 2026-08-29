// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "print_buf.h"

#include <liblv/binding.h>

#include <cassert>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace simple {

PrintBuf::PrintBuf(const sc_module_name &name, uint64_t num_entries,
                   std::string filename = "printf.log")
    : sc_module(name),
      num_entries_(num_entries),
      clock_i_("clock"),
      sink_("sink",
            [this](tlm::tlm_generic_payload &trans) {
              return ReceiveData(&trans);
            }),
      file_(filename, std::ios::out) {
  buf_.reserve(num_entries_);

  if (!file_.is_open()) {
    SC_REPORT_ERROR("PrintBuf", ("Failed to open file: " + filename).c_str());
  }

  SC_METHOD(ProcessMethod);
}

PrintBuf::~PrintBuf() {
  FlushAll();
  CloseFile();
}

void PrintBuf::write_bytes(uint64_t addr, const LuaBytes &data) {
  for (auto &byte : data.value()) {
    buf_[addr].push_back(static_cast<char>(byte));
  }
  FlushBuffer(addr);
}

void PrintBuf::set_clock(sc_clock *clock) {
  clock_ = clock;
  clock_i_.bind(*clock_);
}

sc_clock *PrintBuf::clock() const { return clock_; }

void PrintBuf::FlushBuffer(uint64_t addr) {
  assert(addr < num_entries_);
  if (buf_[addr].length() > 0) {
    std::cout << buf_[addr];
    file_ << buf_[addr] << std::flush;
    buf_[addr].clear();
  }
}

void PrintBuf::FlushAll() {
  for (uint64_t i = 0; i < num_entries_; ++i) {
    FlushBuffer(i);
  }
}

void PrintBuf::CloseFile() {
  if (file_.is_open()) {
    file_.close();
  }
}

void PrintBuf::ProcessMethod() {
  // Check if the sink is ready to process a transaction
  bool is_ready =
      sink_.req_port->num_available() > 0 && sink_.resp_port->num_free() > 0;

  if (!is_ready) {
    // Wait until there is a request and we can put a response
    next_trigger(sink_.req_port->data_written_event() |
                 sink_.resp_port->data_read_event());
    return;
  }

  // Only process on positive clock edge
  if (clock_i_.posedge()) {
    // Process all available transactions in the sink FIFO in the same cycle
    while (sink_.req_port->num_available() > 0 &&
           sink_.resp_port->num_free() > 0) {
      tlm::tlm_generic_payload *trans;
      sink_.req_port->nb_read(trans);  // Read the request transaction
      trans->acquire();
      ReceiveData(trans);
      sink_.resp_port->nb_write(trans);
      trans->release();
    }
  }

  next_trigger(clock_i_.posedge_event());
}

unsigned int PrintBuf::ReceiveData(tlm::tlm_generic_payload *trans) {
  tlm::tlm_command cmd = trans->get_command();
  sc_dt::uint64 addr = trans->get_address();
  unsigned char *ptr = trans->get_data_ptr();
  unsigned int len = trans->get_data_length();
  unsigned char *byte = trans->get_byte_enable_ptr();
  [[maybe_unused]] unsigned int width = trans->get_streaming_width();

  if (cmd == tlm::TLM_WRITE_COMMAND) {
    for (unsigned int i = 0; i < len; i++) {
      if (!byte || byte[i] == TLM_BYTE_ENABLED) {
        buf_[addr].push_back(static_cast<char>(ptr[i]));
        if (ptr[i] == '\n') {
          FlushBuffer(addr);
        }
      }
    }
  } else {
    SC_REPORT_ERROR("TLM-2",
                    "Illegal transaction command received by printf_buf");
  }

  trans->set_response_status(tlm::TLM_OK_RESPONSE);
  return len;
}

LV_BINDING(simple, PrintBuf)
    .constructor(
        [](const char *name, unsigned int num_entries) {
          return std::make_shared<PrintBuf>(name, num_entries);
        },
        lv::params("name", "num_entries"), lv::doc("Create a print buffer"))
    .constructor(
        [](const char *name, unsigned int num_entries,
           std::optional<std::string> filename) {
          if (filename) {
            return std::make_shared<PrintBuf>(name, num_entries, *filename);
          }
          return std::make_shared<PrintBuf>(name, num_entries);
        },
        lv::params("name", "num_entries", "filename"),
        lv::doc("Create a print buffer with an output file"))
    .property("port", &PrintBuf::port, lv::doc("Print buffer request port"))
    .property("clock", &PrintBuf::clock, &PrintBuf::set_clock,
              lv::doc("SystemC clock"))
    .method("write_bytes", &PrintBuf::write_bytes, lv::params("addr", "data"),
            lv::doc("Write bytes into the buffer"))
    .method("flush", &PrintBuf::FlushAll, lv::doc("Flush all buffered text"))
    .method("close_file", &PrintBuf::CloseFile,
            lv::doc("Close the output file"));

}  // namespace simple
