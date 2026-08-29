// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <libcomm/libcomm.h>

#include <array>
#include <cassert>
#include <condition_variable>
#include <cstdio>
#include <vector>

int main(int argc, char *argv[]) {
  srand(time(NULL));
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <socket_path>\n", argv[0]);
    return 1;
  }

  timeval timeout = {1, 0};  // 1-second timeout.
  auto transceiver = libcomm::Connect(argv[1], &timeout, nullptr);
  if (!transceiver) {
    fprintf(stderr, "Cannot connect to %s\n", argv[1]);
    return 1;
  }

  std::array<uint8_t, 256> mem;
  std::vector<size_t> sizes = {1, 2, 4, 8, 16, 32, 64};

  uint32_t probe_id = rand();
  uint32_t terminate_id = rand();

  std::mutex m;

  bool probe_acked = false;
  std::condition_variable probe_ack;

  int resp_cnt = 0;
  bool terminated = false;
  std::condition_variable completion;

  bool terminate_acked = false;
  std::condition_variable terminate_ack;

  transceiver->RegisterSyncHandler([&](auto *self, auto msg) {
    if (msg.is_response()) {
      if (msg.cmd() == libcomm::Cmd::AccessAckData) {
        // 3. When read request is responded, send the write request to copy the
        //    data.
        assert(msg.id() == msg.addr());
        auto addr = 128 + msg.size();
        self->Send(libcomm::Msg::Build(libcomm::Cmd::Put)
                       .addr(addr)
                       .size(msg.size())
                       .id(addr)
                       .data(msg.data()));
      } else if (msg.cmd() == libcomm::Cmd::ProbeAck) {
        assert(msg.id() == probe_id);
        std::lock_guard<std::mutex> lock(m);
        probe_acked = true;
        probe_ack.notify_one();
      } else if (msg.cmd() == libcomm::Cmd::TerminateAck) {
        assert(msg.id() == terminate_id);
        std::lock_guard<std::mutex> lock(m);
        terminate_acked = true;
        terminate_ack.notify_one();
      } else if (msg.cmd() == libcomm::Cmd::AccessAck) {
        std::lock_guard<std::mutex> lock(m);
        assert(msg.id() == msg.addr());
        ++resp_cnt;
        completion.notify_one();
      }
    }

    if (msg.is_request()) {
      if (msg.cmd() == libcomm::Cmd::Get) {
        self->Send(libcomm::Msg::Respond(msg).data(&mem[msg.addr()]));
      } else if (msg.cmd() == libcomm::Cmd::Put) {
        memcpy(&mem[msg.addr()], msg.data(), msg.size());
        self->Send(libcomm::Msg::Respond(msg));
      } else if (msg.cmd() == libcomm::Cmd::Terminate) {
        self->Send(libcomm::Msg::Respond(msg));
        std::lock_guard<std::mutex> lock(m);
        terminated = true;
        completion.notify_one();
      }
    }
  });

  // 0. Initialize the memory.
  for (uint8_t &b : mem) {
    b = rand();
  }

  // 1. Probe the device.
  {
    transceiver->Send(libcomm::Msg::Build(libcomm::Cmd::Probe).id(probe_id));
    std::unique_lock<std::mutex> lock(m);
    probe_ack.wait(lock, [&] {
      return probe_acked;
    });
  }

  // 2. Send all read requests
  for (size_t size : sizes) {
    auto addr = size;  // Using size as address.
    transceiver->Send(libcomm::Msg::Build(libcomm::Cmd::Get)
                          .id(addr)  // Using address as id.
                          .addr(addr)
                          .size(size)
                          .data(nullptr));
  }

  // 4. Wait for completion: got all response and terminate request.
  {
    std::unique_lock<std::mutex> lock(m);
    completion.wait(lock, [&] {
      return resp_cnt == sizes.size() && terminated;
    });
  }

  // 5. Terminate the device.
  {
    transceiver->Send(
        libcomm::Msg::Build(libcomm::Cmd::Terminate).id(terminate_id));
    std::unique_lock<std::mutex> lock(m);
    terminate_ack.wait(lock, [&] {
      return terminate_acked;
    });
  }

  // 6. Compare the memory.
  for (size_t i = 1; i < 128; ++i) {
    printf("Host: 0x%02zx: 0x%02x == 0x%02x\n", i, mem[i], mem[128 + i]);
    assert(mem[i] == mem[128 + i]);
  }
  printf("Host: pass!!\n");
  return 0;
}
