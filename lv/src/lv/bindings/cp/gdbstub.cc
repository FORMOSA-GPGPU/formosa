// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "gdbstub.h"

#include <arpa/inet.h>
#include <fmt/core.h>
#include <liblv/log.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <stdexcept>
#include <string>
#include <tuple>

#define LOG(fmt_str, ...)                  \
  do {                                     \
    if (enable_log) {                      \
      lv::Println(fmt_str, ##__VA_ARGS__); \
    }                                      \
  } while (0)

namespace cp {
GDBStub::GDBStub(bool init, int port, bool log)
    : enable_log(log), server_fd(-1), client_fd(-1), need_resp(false) {
  if (init) {
    connect(port);
  }
}

bool GDBStub::connected() const { return port_connected; }

void GDBStub::connect(int port) {
  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd == -1) {
    throw std::runtime_error("Failed to create GDBStub server socket");
  }

  sockaddr_in server_addr = {};
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(port);

  int res =
      bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
  if (res == -1) {
    throw std::runtime_error("Failed to bind socket.");
  }

  res = listen(server_fd, 1);
  if (res == -1) {
    throw std::runtime_error("Failed to listen on socket.");
  }

  LV_INFO("Waiting for GDB connection on port {}", port);
  client_fd = accept(server_fd, nullptr, nullptr);
  if (client_fd == -1) {
    throw std::runtime_error("Failed to accept connections");
  }

  // GDB protocol operates with small packets, and thus we need to disable
  // the sending delay (Nagle's Algorithm). Otherwise gdb commands would have
  // noticeable lag.
  int flag = 1;
  setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY,
             reinterpret_cast<char *>(&flag), sizeof(int));

  LV_INFO("GDB client connected");
  port_connected = true;
}

GDBStub::~GDBStub() {
  if (client_fd != -1) {
    close(client_fd);
  }
  if (server_fd != -1) {
    close(server_fd);
  }
}

void GDBStub::set_debug_if(debug_if *if_) { debug_ = if_; }

std::string GDBStub::receive_packet() {
  char buffer[4096];
  std::string packet;
  bool found_start = false;
  LOG("Waiting for next packet");
  while (true) {
    ssize_t len = recv(client_fd, buffer, sizeof(buffer), 0);
    if (len < 0) {
      throw std::runtime_error("Connection closed or error receiving");
    }
    for (ssize_t i = 0; i < len; i++) {
      char c = buffer[i];
      if (!found_start && c == '$') {
        found_start = true;
        packet.clear();
        continue;
      }

      if (c == '#') {
        LOG(">{}", packet);
        send_ack();
        return packet;
      }
      packet += c;
    }
  }
}

void GDBStub::send_ack() {
  int res = send(client_fd, "+", 1, 0);
  if (res == -1) {
    throw std::runtime_error("Failed to send acknowledgment.");
  }
  LOG("<+");
}

void GDBStub::send_packet(const std::string &packet) {
  uint8_t checksum = 0;
  for (auto &c : packet) {
    checksum += c;
  }
  std::string resp = fmt::format("${}#{:02x}", packet, checksum & 0xff);
  int res = send(client_fd, resp.c_str(), resp.size(), 0);
  if (res == -1) {
    throw std::runtime_error("Failed to send packet to GDB");
  }
  LOG("<{}", resp);
  wait_ack();
}

void GDBStub::wait_ack() {
  char ack;
  size_t len = recv(client_fd, &ack, 1, 0);
  if (len <= 0) {
    throw std::runtime_error("Failed to receive ack");
  }

  if (ack != '+') {
    throw std::runtime_error("Should receive ack '+'");
  }
  LOG(">+");
}

#define CMD_NOT_SUPPORTED send_packet("")

bool GDBStub::handle_packet(const std::string &packet) {
  switch (packet[0]) {
    default: {
      CMD_NOT_SUPPORTED;
      return false;
    }

    case '?':
      send_packet("S05");  // real halt reason
      return false;

    case 'g': {
      // Read all registers
      std::array<int64_t, 32> reg = debug_->get_registers();
      uint64_t pc = debug_->get_program_counter();
      std::string res = serialize_regs(reg, pc);
      send_packet(res);
      return false;
    }

    case 'Z': {
      if (packet[1] == '0' || packet[1] == '1') {
        // Insert HW breakpoint
        uint64_t addr =
            std::stoull(packet.substr(3, packet.find(',', 3)), nullptr, 16);
        debug_->insert_breakpoint(addr);
        send_packet("OK");
        return false;
      } else {
        CMD_NOT_SUPPORTED;
        return false;
      }
    }

    case 'z': {
      if (packet[1] == '0' || packet[1] == '1') {
        // Delete HW breakpoint
        uint64_t addr =
            std::stoull(packet.substr(3, packet.find(',', 3)), nullptr, 16);
        debug_->remove_breakpoint(addr);
        send_packet("OK");
        return false;
      } else {
        CMD_NOT_SUPPORTED;
        return false;
      }
    }

    case 'q': {
      if (packet.find("qSupported") == 0) {
        send_packet("PacketSize=4096");
      } else if (packet.find("qOffsets") == 0) {
        send_packet("Text=0;Data=0;Bss=0");
      } else {
        CMD_NOT_SUPPORTED;
      }
      return false;
    }

    case 'G': {
      auto write_val = deserialize_regs(packet.substr(1));
      for (int i = 1; i < 32; i++) {
        debug_->write_register(i, write_val[i]);
      }
      send_packet("OK");
      return false;
    }

    case 'm': {
      const auto req = parse_mem_req(packet);
      std::string res;
      for (size_t i = 0; i < std::get<1>(req); i++) {
        res += serialize_byte(debug_->load_byte(std::get<0>(req) + i));
      }
      send_packet(res);
      return false;
    }

    case 'M': {
      const auto req = parse_mem_req(packet);
      std::string data_str = packet.substr(packet.find(':') + 1);
      for (size_t i = 0; i < std::get<1>(req); i++) {
        uint8_t val = std::stoul(data_str.substr(i * 2, 2), nullptr, 16);
        debug_->store_byte(std::get<0>(req), val);
      }
      return false;
    }

    case 's': {
      // Will respond when next breakpoint occurs
      debug_->set_single_step();
      debug_->set_status(rv64::CoreExecStatus::Runnable);
      need_resp = true;
      return true;
    }

    case 'c': {
      debug_->set_status(rv64::CoreExecStatus::Runnable);
      need_resp = true;
      return true;
    }
  }
  return true;
}

std::string GDBStub::serialize_regs(const std::array<int64_t, 32> &reg,
                                    uint64_t pc) {
  std::string res;
  uint64_t temp;
  for (auto &r : reg) {
    temp = r;
    for (int i = 0; i < 8; i++) {
      res += serialize_byte(temp & 0xff);
      temp >>= 8;
    }
  }

  temp = pc;
  for (int i = 0; i < 8; i++) {
    res += serialize_byte(temp & 0xff);
    temp >>= 8;
  }
  return res;
}

std::array<int64_t, 32> GDBStub::deserialize_regs(const std::string &data) {
  std::array<int64_t, 32> res = {0};
  for (int i = 1; i < 32; i++) {
    int64_t temp = 0;
    for (int j = 0; j < 8; j++) {
      temp |= std::stoull(data.substr(i * 16 + j * 2, 2));
      temp <<= 8;
    }
    res[i] = temp;
  }
  return res;
}

std::tuple<uint64_t, size_t> GDBStub::parse_mem_req(const std::string &packet) {
  size_t comma_pos = packet.find(',');
  size_t colon_pos = packet.find(':');
  if (comma_pos == std::string::npos) {
    throw std::invalid_argument("memory packet");
  }

  /*
    colon_pos is used becasue `M` packets has one. For `m` packets colon_pos
    is simply std::string::npos.
  */
  uint64_t addr = std::stoull(packet.substr(1, comma_pos - 1), nullptr, 16);
  size_t size =
      std::stoull(packet.substr(comma_pos + 1, colon_pos), nullptr, 16);
  return std::make_tuple(addr, size);
}

std::string GDBStub::serialize_byte(uint8_t val) {
  return fmt::format("{:02x}", val);
}

void GDBStub::gdb_main() {
  /* Send response */
  if (need_resp) {
    send_packet("S05");
    need_resp = false;
  }

  while (true) {
    auto packet = receive_packet();
    if (handle_packet(packet)) break;
  }
}

}  // namespace cp
