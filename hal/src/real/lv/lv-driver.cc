// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <formosa_addr_map.h>
#include <libcomm/libcomm.h>
#include <real/real.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/util.h"

struct LvClient {
  struct StoredResponse {
    libcomm::Status status;
    std::vector<uint8_t> data;
  };

  uint64_t msg_id = 0;
  std::unordered_map<uint64_t, StoredResponse> response;
  std::mutex mtx;
  std::mutex send_mtx;
  std::condition_variable cv;
  std::unique_ptr<libcomm::Transceiver> transceiver;
} client;

namespace {

int fsa_real_put(uintptr_t dev_addr, const void *host_ptr, size_t size) {
  if (!client.transceiver || !client.transceiver->IsConnectionAlive()) {
    return -1;  // Return error if not connected
  }
  if (host_ptr == nullptr) {
    return -1;
  }
  if (size == 0) {
    return 0;  // No data to copy
  }

  uint32_t msg_id;
  {
    std::lock_guard<std::mutex> lk(client.mtx);
    msg_id = client.msg_id++;  // thread-safe increment
  }

  libcomm::Msg msg = libcomm::Msg::Build(libcomm::Cmd::Put)
                         .id(msg_id)
                         .addr(dev_addr)
                         .size(size)
                         .data(reinterpret_cast<const uint8_t *>(host_ptr));
  {
    std::lock_guard<std::mutex> lk(client.send_mtx);
    CHECK_ERR(client.transceiver->Send(msg));
  }

  std::unique_lock<std::mutex> lk(client.mtx);
  if (!client.cv.wait_for(lk, std::chrono::seconds(5), [&] {
        return client.response.find(msg_id) != client.response.end();
      })) {
    fprintf(stderr, "Timeout waiting for put response (id=%u)\n", msg_id);
    return -1;
  }

  auto it = client.response.find(msg_id);
  auto response = std::move(it->second);
  client.response.erase(it);

  if (response.status != libcomm::Status::Okay) {
    return -1;
  }
  return 0;
}

int fsa_real_get(uintptr_t dev_addr, void *host_ptr, size_t size) {
  if (!client.transceiver || !client.transceiver->IsConnectionAlive()) {
    return -1;  // Return error if not connected
  }
  if (host_ptr == nullptr) {
    return -1;
  }
  if (size == 0) {
    return 0;  // No data to copy
  }

  uint32_t msg_id;
  {
    std::lock_guard<std::mutex> lk(client.mtx);
    msg_id = client.msg_id++;  // thread-safe increment
  }

  libcomm::Msg msg = libcomm::Msg::Build(libcomm::Cmd::Get)
                         .id(msg_id)
                         .addr(dev_addr)
                         .size(size);
  {
    std::lock_guard<std::mutex> lk(client.send_mtx);
    CHECK_ERR(client.transceiver->Send(msg));
  }

  std::unique_lock<std::mutex> lk(client.mtx);
  if (!client.cv.wait_for(lk, std::chrono::seconds(5), [&] {
        return client.response.find(msg_id) != client.response.end();
      })) {
    fprintf(stderr, "Timeout waiting for get response (id=%u)\n", msg_id);
    return -1;
  }

  auto it = client.response.find(msg_id);
  auto response = std::move(it->second);
  client.response.erase(it);

  if (response.status != libcomm::Status::Okay) {
    return -1;
  }
  if (response.data.size() != size) {
    return -1;
  }
  std::memcpy(host_ptr, response.data.data(), size);
  return 0;
}

}  // namespace

int fsa_real_mmio(uint64_t offset, int64_t wr_val, uint64_t *rd_ptr) {
  const auto *config = formosa::real::configuration_snapshot();
  if (config == nullptr) return -1;
  if (rd_ptr == nullptr) {
    // Write request
    return fsa_real_put(config->fsa_mmio_base + offset, &wr_val, 8);
  } else {
    // Read request
    return fsa_real_get(config->fsa_mmio_base + offset, rd_ptr, 8);
  }
}

int fsa_real_cp_reset() {
  const auto *config = formosa::real::configuration_snapshot();
  if (config == nullptr) return -1;
  /* Assert level-triggered MSIP.  Firmware clears the bit in the ISR
   * (or ROM reset_handler). */
  uint32_t reset = 1;
  return fsa_real_put(config->clint_base + FSA_CLINT_MSIP_OFFSET, &reset,
                      sizeof(reset));
}

int fsa_real_copy_to_scratchpad(uintptr_t dev_addr, const void *host_ptr,
                                size_t size) {
  const auto *config = formosa::real::configuration_snapshot();
  if (config == nullptr) return -1;
  uint64_t block_size = config->cache_line_size;
  if (block_size == 0) {
    return -1;
  }
  for (uint64_t begin_addr = dev_addr; begin_addr < dev_addr + size;) {
    uint64_t next_block_addr = (begin_addr / block_size + 1) * block_size;
    uint64_t end_addr =
        std::min(next_block_addr, static_cast<uint64_t>(dev_addr + size));
    int status = fsa_real_put(
        begin_addr,
        reinterpret_cast<const uint8_t *>(host_ptr) + (begin_addr - dev_addr),
        end_addr - begin_addr);
    if (status != 0) {
      return status;
    }
    begin_addr = end_addr;
  }
  return 0;
}

int fsa_real_copy_from_scratchpad(uintptr_t dev_addr, void *host_ptr,
                                  size_t size) {
  const auto *config = formosa::real::configuration_snapshot();
  if (config == nullptr) return -1;
  uint64_t block_size = config->cache_line_size;
  if (block_size == 0) {
    return -1;
  }
  for (uint64_t begin_addr = dev_addr; begin_addr < dev_addr + size;) {
    uint64_t next_block_addr = (begin_addr / block_size + 1) * block_size;
    uint64_t end_addr =
        std::min(next_block_addr, static_cast<uint64_t>(dev_addr + size));
    int status = fsa_real_get(
        begin_addr,
        reinterpret_cast<uint8_t *>(host_ptr) + (begin_addr - dev_addr),
        end_addr - begin_addr);
    if (status != 0) {
      return status;
    }
    begin_addr = end_addr;
  }
  return 0;
}

int fsa_real_probe() {
  struct timeval timeout;
  timeout.tv_sec = 0;
  timeout.tv_usec = 100000;  // 100ms
  int err = -2;              // -2 indicates probe in progress
  auto probe_transceiver =
      libcomm::Connect(getenv("AGENT_SOCKET_PATH"), &timeout, []() {});
  if (!probe_transceiver) {
    fprintf(stderr, "Failed to connect to agent socket\n");
    return -1;
  }
  std::mutex probe_mtx;
  std::condition_variable probe_cv;
  probe_transceiver->RegisterSyncHandler(
      [&](libcomm::Transceiver *self, const libcomm::Msg &msg) {
        if (msg.is_response() && msg.cmd() == libcomm::Cmd::ProbeAck) {
          std::unique_lock<std::mutex> lk(probe_mtx);
          if (msg.status() != libcomm::Status::Okay) {
            fprintf(stderr, "Probe failed\n");
            err = -1;
          } else {
            err = 0;
          }
          probe_cv.notify_one();
        }
      });
  libcomm::Msg probe_msg = libcomm::Msg::Build(libcomm::Cmd::Probe);
  CHECK_ERR(probe_transceiver->Send(probe_msg));

  std::unique_lock<std::mutex> lk(probe_mtx);
  if (!probe_cv.wait_for(lk, std::chrono::seconds(2), [&] {
        return err != -2;
      })) {
    return -1;
  }

  return err;
}

int fsa_real_init() {
  int retries = 5;
  while (retries-- > 0) {
    client.transceiver = libcomm::Connect(getenv("AGENT_SOCKET_PATH"), nullptr);
    if (client.transceiver) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  if (!client.transceiver) {
    fprintf(stderr, "Failed to connect to agent socket\n");
    return -1;
  }
  client.transceiver->RegisterSyncHandler([&](libcomm::Transceiver *self,
                                              const libcomm::Msg &msg) {
    if (msg.is_request()) {
      if (msg.cmd() == libcomm::Cmd::Get) {
        std::unique_ptr<uint8_t[]> data =
            std::make_unique<uint8_t[]>(msg.size());
        memcpy(data.get(), reinterpret_cast<const uint8_t *>(msg.addr()),
               msg.size());
        libcomm::Msg resp = libcomm::Msg::Respond(msg).data(data.get());
        std::lock_guard<std::mutex> lk(client.send_mtx);
        self->Send(resp);
      } else if (msg.cmd() == libcomm::Cmd::Put) {
        memcpy(reinterpret_cast<void *>(msg.addr()), msg.data(), msg.size());
        libcomm::Msg resp = libcomm::Msg::Respond(msg);
        std::lock_guard<std::mutex> lk(client.send_mtx);
        self->Send(resp);
      }
    }
    if (msg.is_response()) {
      std::unique_lock<std::mutex> lk(client.mtx);

      std::vector<uint8_t> data_copy;
      if (msg.has_data() && msg.size() > 0) {
        data_copy.assign(msg.data(), msg.data() + msg.size());
      }

      LvClient::StoredResponse resp;
      resp.status = msg.status();
      resp.data = std::move(data_copy);

      client.response.emplace(msg.id(), std::move(resp));
      client.cv.notify_one();
    }
  });
  return 0;
}

void fsa_real_abort() {
  client.transceiver.reset();
  std::lock_guard<std::mutex> lk(client.mtx);
  client.response.clear();
  client.msg_id = 0;
}

void fsa_real_cleanup() {
  if (client.transceiver) {
    libcomm::Msg term_msg = libcomm::Msg::Build(libcomm::Cmd::Terminate).id(0);
    {
      std::lock_guard<std::mutex> lk(client.send_mtx);
      client.transceiver->Send(term_msg);
    }
  }
  fsa_real_abort();
}
