/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <fmt/core.h>

#include <string>
#include <tuple>

#include "rv64/debug_if.h"

namespace cp {

class GDBStub {
 public:
  GDBStub() = delete;
  GDBStub(bool init, int port, bool log);
  ~GDBStub();
  void set_debug_if(debug_if *);
  void gdb_main();
  void connect(int port);
  bool connected() const;

 private:
  debug_if *debug_ = nullptr;
  bool enable_log = false;
  int server_fd;
  int client_fd;
  bool need_resp;
  bool port_connected = false;

  void log(const char *fmt...);
  void send_packet(const std::string &packet);
  void send_ack();
  void wait_ack();
  std::string receive_packet();
  bool handle_packet(const std::string &packet);
  std::string serialize_regs(const std::array<int64_t, 32> &reg, uint64_t pc);
  std::string serialize_byte(uint8_t val);
  std::array<int64_t, 32> deserialize_regs(const std::string &data);
  std::tuple<uint64_t, size_t> parse_mem_req(const std::string &packet);
};

}  // namespace cp
