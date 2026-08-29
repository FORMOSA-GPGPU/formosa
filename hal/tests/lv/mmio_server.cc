// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <addr_map/formosa_addr_map.h>
#include <libcomm/libcomm.h>
#include <real/real.h>

#include <algorithm>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include "command_packet.h"

// Global map to store received MMIO writes
std::map<uint64_t, uint64_t> mmio_registers;
std::vector<std::unique_ptr<libcomm::Transceiver>> transceivers;

/* LV-only injectors: write 1 then clear. Addresses sit outside the command
 * ring / completion pool used by the mock. */
constexpr uint64_t kEnableFailNextCmdPacket = 0x20000FF0ULL;
constexpr uint64_t kEnableFailNextWrPtr = 0x20000FF8ULL;
constexpr uint64_t kEnableFreeRingOnComplete = 0x20000FE8ULL;
bool fail_next_cmd_packet = false;
bool fail_next_wr_ptr = false;
bool free_ring_on_complete = false;
bool fail_next_init_cmd_ring_base = false;

uint64_t test_fsa_mmio_base() {
  const auto *config = formosa::real::configuration_snapshot();
  return config == nullptr ? FSA_MMIO_BASE : config->fsa_mmio_base;
}

uint64_t test_clint_base() {
  const auto *config = formosa::real::configuration_snapshot();
  return config == nullptr ? FSA_CLINT_BASE : config->clint_base;
}

// Helper function to get register / memory name from address
const char *get_name(uint64_t addr) {
  if (addr == test_fsa_mmio_base() + FSA_CP_OFF_CMD_RING_BASE)
    return "mmio_cmd_ring_base";
  if (addr == test_fsa_mmio_base() + FSA_CP_OFF_CMD_SIZE)
    return "mmio_cmd_cmd_size";
  if (addr == test_fsa_mmio_base() + FSA_CP_OFF_CMD_RING_SIZE)
    return "mmio_cmd_ring_size";
  if (addr == test_fsa_mmio_base() + FSA_CP_OFF_RD_PTR)
    return "mmio_cmd_rd_ptr";
  if (addr == test_fsa_mmio_base() + FSA_CP_OFF_WR_PTR)
    return "mmio_cmd_wr_ptr";
  if (addr >= mmio_registers[test_fsa_mmio_base() + FSA_CP_OFF_CMD_RING_BASE] &&
      addr <
          mmio_registers[test_fsa_mmio_base() + FSA_CP_OFF_CMD_RING_BASE] +
              mmio_registers[test_fsa_mmio_base() + FSA_CP_OFF_CMD_RING_SIZE] *
                  sizeof(Packet))
    return "command ring";
  return "UNKNOWN_ADDRESS";
}

// Function to initialize MMIO registers with default values
void initialize_mmio_registers() {
  const char *fail_init = std::getenv("LV_FORMOSA_FAIL_INIT_ONCE");
  fail_next_init_cmd_ring_base =
      fail_init != nullptr && std::strcmp(fail_init, "1") == 0;
  mmio_registers[test_fsa_mmio_base() + FSA_CP_OFF_CMD_RING_SIZE] =
      8;  // Example: 8 packets
  mmio_registers[test_fsa_mmio_base() + FSA_CP_OFF_CMD_RING_BASE] =
      0x20000010;  // misaligned base address
  mmio_registers[test_fsa_mmio_base() + FSA_CP_OFF_FW_STATUS] =
      kFirmwareStatusReset;
  mmio_registers[test_fsa_mmio_base() + FSA_CP_OFF_FW_ABI_VERSION] = 0;
  mmio_registers[test_fsa_mmio_base() + FSA_CP_OFF_FW_BOOT_GENERATION] = 0;
  mmio_registers[test_fsa_mmio_base() + FSA_CP_OFF_FW_FAULT_CODE] =
      kFirmwareFaultNone;
}

void store_put_data(uint64_t addr, const uint8_t *data, size_t size) {
  // Model scratchpad as 8-byte-addressable cells for this test server.
  for (size_t i = 0; i < size; i += sizeof(uint64_t)) {
    uint64_t val = 0;
    size_t chunk = std::min(sizeof(uint64_t), size - i);
    std::memcpy(&val, data + i, chunk);
    mmio_registers[addr + i] = val;
  }
}

uint64_t load_cell(uint64_t addr) {
  const auto it = mmio_registers.find(addr);
  return it == mmio_registers.end() ? 0 : it->second;
}

void complete_token(FsaCompletionToken token, FsaCompletionResult result) {
  if (!fsa_completion_token_has_valid_slot(token)) return;
  /* Mock does not model firmware; terminal-publish so HAL lifecycle tests
   * exercise token/tag validation. */
  const uint64_t slot_addr =
      FSA_COMPLETION_POOL_BASE +
      fsa_completion_token_slot_index(token) * FSA_COMPLETION_SLOT_BYTES;
  mmio_registers[slot_addr] =
      fsa_completion_slot_encode(fsa_completion_token_alloc_tag(token), result);
}

void track_completion_from_cmd(uint64_t new_wr_ptr) {
  const uint64_t cmd_ring_size =
      mmio_registers[test_fsa_mmio_base() + FSA_CP_OFF_CMD_RING_SIZE];
  const uint64_t cmd_ring_base =
      mmio_registers[test_fsa_mmio_base() + FSA_CP_OFF_CMD_RING_BASE];
  if (cmd_ring_size == 0) return;

  // host writes cmd payload first, then advances WR_PTR to next slot.
  const uint64_t cmd_slot = (new_wr_ptr + cmd_ring_size - 1) % cmd_ring_size;
  const uint64_t cmd_addr = cmd_ring_base + cmd_slot * sizeof(Packet);
  const uint16_t header =
      static_cast<uint16_t>(mmio_registers[cmd_addr] & 0xFFFF);
  const bool known_header = (header >= kKernelDispatchPacketHeader &&
                             header <= kBarrierPacketHeader) ||
                            header == kMemoryCopyPacketHeader;
  const FsaCompletionToken token =
      load_cell(cmd_addr + FSA_COMPLETION_TOKEN_OFFSET);
  complete_token(token, known_header
                            ? FSA_COMPLETION_RESULT_SUCCESS
                            : FSA_COMPLETION_RESULT_COMMAND_FAILURE_MIN);
  /* Optional instant ring drain (enabled by tests that need pool depth >
   * ring). */
  if (free_ring_on_complete) {
    mmio_registers[test_fsa_mmio_base() + FSA_CP_OFF_RD_PTR] = new_wr_ptr;
  }
}

void handle_mmio_message(libcomm::Transceiver *self, const libcomm::Msg &msg) {
  if (msg.cmd() == libcomm::Cmd::Put) {
    uint64_t addr = msg.addr();
    uint64_t value = 0;
    std::memcpy(&value, msg.data(), std::min(sizeof(value), msg.size()));

    if (addr == kEnableFailNextCmdPacket) {
      fail_next_cmd_packet = value != 0;
      self->Send(libcomm::Msg::Respond(msg));
      return;
    }
    if (addr == kEnableFailNextWrPtr) {
      fail_next_wr_ptr = value != 0;
      self->Send(libcomm::Msg::Respond(msg));
      return;
    }
    if (addr == kEnableFreeRingOnComplete) {
      free_ring_on_complete = value != 0;
      self->Send(libcomm::Msg::Respond(msg));
      return;
    }
    const uint64_t cmd_ring_base =
        mmio_registers[test_fsa_mmio_base() + FSA_CP_OFF_CMD_RING_BASE];
    const uint64_t cmd_ring_size =
        mmio_registers[test_fsa_mmio_base() + FSA_CP_OFF_CMD_RING_SIZE];
    const uint64_t cmd_ring_end =
        cmd_ring_base + cmd_ring_size * sizeof(Packet);
    if (fail_next_cmd_packet && addr >= cmd_ring_base && addr < cmd_ring_end) {
      fail_next_cmd_packet = false;
      std::cout << "[MMIO Server] Injected pre-WP command-packet failure\n";
      self->Send(
          libcomm::Msg::Respond(msg).status(libcomm::Status::GenericErr));
      return;
    }

    store_put_data(addr, msg.data(), msg.size());
    std::cout << "[MMIO Server] Received Write: Address = 0x" << std::hex
              << addr << " (" << get_name(addr) << ")"
              << ", Size = " << msg.size() << std::dec << std::endl;
    for (size_t i = 0; i < msg.size(); i += 8) {
      uint64_t val = *reinterpret_cast<const uint64_t *>(msg.data() + i);
      std::cout << "[MMIO Server]                 Data[" << i / 8 << "] = 0x"
                << std::hex << val << std::dec << std::endl;
    }

    if (addr == (test_fsa_mmio_base() + FSA_CP_OFF_WR_PTR)) {
      uint64_t wr_ptr = value;
      std::cout << "[MMIO Server] Command buffer write pointer updated to: "
                << wr_ptr << std::endl;
      track_completion_from_cmd(wr_ptr);
      if (fail_next_wr_ptr) {
        fail_next_wr_ptr = false;
        std::cout << "[MMIO Server] Injected post-apply WP failure\n";
        self->Send(
            libcomm::Msg::Respond(msg).status(libcomm::Status::GenericErr));
        return;
      }
    }

    if (addr == test_clint_base() + FSA_CLINT_MSIP_OFFSET && value == 1) {
      /* Cooperative Firmware Reboot: runtime drains then returns to ROM.
       * HAL waits for Reset before publishing a new Boot Descriptor. */
      mmio_registers[test_fsa_mmio_base() + FSA_CP_OFF_FW_STATUS] =
          kFirmwareStatusReset;
      mmio_registers[test_fsa_mmio_base() + FSA_CP_OFF_FW_HOST_ADDR] = 0;
      mmio_registers[test_fsa_mmio_base() + FSA_CP_OFF_FW_SIZE] = 0;
      mmio_registers[test_fsa_mmio_base() + FSA_CP_OFF_RD_PTR] = 0;
      mmio_registers[test_fsa_mmio_base() + FSA_CP_OFF_WR_PTR] = 0;
    }

    /* Boot Descriptor: model ROM Host-DMA upload then READY handshake. */
    if (addr == (test_fsa_mmio_base() + FSA_CP_OFF_FW_SIZE) && value != 0) {
      mmio_registers[addr] = 0;
      mmio_registers[test_fsa_mmio_base() + FSA_CP_OFF_FW_HOST_ADDR] = 0;
      uint64_t previous_generation =
          load_cell(test_fsa_mmio_base() + FSA_CP_OFF_FW_BOOT_GENERATION);
      uint64_t next_generation =
          previous_generation == UINT64_MAX ? 1 : previous_generation + 1;
      if (next_generation == 0) next_generation = 1;
      mmio_registers[test_fsa_mmio_base() + FSA_CP_OFF_FW_BOOT_GENERATION] =
          next_generation;
      mmio_registers[test_fsa_mmio_base() + FSA_CP_OFF_FW_STATUS] =
          kFirmwareStatusReady;
      mmio_registers[test_fsa_mmio_base() + FSA_CP_OFF_FW_ABI_VERSION] =
          FSA_COMMAND_ABI_VERSION_V3;
      mmio_registers[test_fsa_mmio_base() + FSA_CP_OFF_FW_FAULT_CODE] =
          kFirmwareFaultNone;
    }

    libcomm::Msg resp = libcomm::Msg::Respond(msg);
    self->Send(resp);
  } else if (msg.cmd() == libcomm::Cmd::Probe) {
    std::cout << "[MMIO Server] Received Probe command." << std::endl;
    libcomm::Msg resp =
        libcomm::Msg::Build(libcomm::Cmd::ProbeAck).id(msg.id());
    self->Send(resp);
    std::cout << "[MMIO Server] Sent ProbeAck response." << std::endl;
  } else if (msg.cmd() == libcomm::Cmd::Get) {
    if (fail_next_init_cmd_ring_base &&
        msg.addr() == test_fsa_mmio_base() + FSA_CP_OFF_CMD_RING_BASE) {
      fail_next_init_cmd_ring_base = false;
      std::cout << "[MMIO Server] Injected late init failure\n";
      self->Send(
          libcomm::Msg::Respond(msg).status(libcomm::Status::GenericErr));
      return;
    }
    std::cout << "[MMIO Server] Received Get command for address 0x" << std::hex
              << msg.addr() << " (" << get_name(msg.addr()) << ")" << std::dec
              << std::endl;
    uint64_t value = load_cell(msg.addr());
    std::vector<uint8_t> response_data(msg.size(), 0);
    for (size_t i = 0; i < response_data.size(); i += sizeof(uint64_t)) {
      const uint64_t cell = load_cell(msg.addr() + i);
      const size_t chunk = std::min(sizeof(uint64_t), response_data.size() - i);
      std::memcpy(response_data.data() + i, &cell, chunk);
    }
    libcomm::Msg resp = libcomm::Msg::Respond(msg)
                            .data(response_data.data())
                            .size(response_data.size());
    self->Send(resp);
    std::cout << "[MMIO Server] Sent Get response with value 0x" << std::hex
              << value << std::dec << std::endl;
  } else if (msg.cmd() == libcomm::Cmd::Terminate) {
    std::cout << "[MMIO Server] Received Terminate command." << std::endl;
    libcomm::Msg resp = libcomm::Msg::Respond(msg);
    self->Send(resp);
    exit(0);
  } else {
    std::cout << "[MMIO Server] Received unknown command: "
              << static_cast<int>(msg.cmd()) << std::endl;
    libcomm::Msg resp = libcomm::Msg::Respond(msg);
    self->Send(resp);
  }
}

int main() {
  initialize_mmio_registers();  // Initialize MMIO registers

  const char *socket_path = std::getenv("AGENT_SOCKET_PATH");
  if (socket_path == nullptr || socket_path[0] == '\0') {
    std::cerr << "[MMIO Server] AGENT_SOCKET_PATH is not set" << std::endl;
    return -1;
  }
  std::unique_ptr<std::thread> accept_thread =
      libcomm::Serve(socket_path, nullptr,
                     [](std::unique_ptr<libcomm::Transceiver> transceiver) {
                       transceiver->RegisterSyncHandler(handle_mmio_message);
                       transceivers.push_back(std::move(transceiver));
                     });

  if (!accept_thread) {
    std::cerr << "[MMIO Server] Cannot open the server on " << socket_path
              << std::endl;
    return -1;
  }

  std::cout << "[MMIO Server] Listening on " << socket_path << std::endl;
  std::cout << "[MMIO Server] Press Ctrl+C to exit." << std::endl;

  accept_thread->join();

  return 0;
}
