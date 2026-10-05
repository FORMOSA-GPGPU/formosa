// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
// SPDX-License-Identifier: Apache-2.0

#include <libcomm/libcomm.h>

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <string_view>

int main(int argc, char **argv) {
  if (argc != 4) return 1;
  const bool disconnect = std::string_view(argv[3]) == "disconnect";
  auto peer = libcomm::Connect(argv[1], nullptr);
  if (!peer) return 2;
  const auto delay = std::chrono::milliseconds(std::atoi(argv[2]));
  std::mutex mutex;
  std::condition_variable cv;
  bool received = false;
  bool reverse_received = false;
  bool okay = true;
  uint8_t byte = 42;
  const auto handler = [&](auto *self, auto msg) {
    if (msg.is_request()) {
      if (disconnect) {
        std::lock_guard<std::mutex> lock(mutex);
        reverse_received = true;
        cv.notify_one();
        return;
      }
      std::this_thread::sleep_for(delay);
      self->Send(libcomm::Msg::Respond(msg).data(&byte));
      return;
    }
    std::lock_guard<std::mutex> lock(mutex);
    okay &= msg.status() == libcomm::Status::Okay;
    if (msg.cmd() == libcomm::Cmd::WaitAck) okay &= msg.addr() == 650;
    if (msg.has_data()) okay &= msg.size() == 1 && msg.data()[0] == 42;
    received = true;
    cv.notify_one();
  };
  peer->RegisterSyncHandler(handler);
  auto exchange = [&](const libcomm::Msg &msg) {
    std::unique_lock<std::mutex> lock(mutex);
    received = false;
    if (peer->Send(msg) != 0) return false;
    return cv.wait_for(lock, std::chrono::seconds(5), [&] {
      return received;
    }) && okay;
  };
  if (!exchange(libcomm::Msg::Build(libcomm::Cmd::Probe))) return 3;
  if (disconnect) {
    if (peer->Send(libcomm::Msg::Build(libcomm::Cmd::Wait).addr(500)) != 0)
      return 6;
    {
      std::unique_lock<std::mutex> lock(mutex);
      if (!cv.wait_for(lock, std::chrono::seconds(1), [&] {
            return reverse_received;
          }))
        return 7;
    }
    peer.reset();
    peer = libcomm::Connect(argv[1], nullptr);
    if (!peer) return 8;
    peer->RegisterSyncHandler(handler);
    if (!exchange(libcomm::Msg::Build(libcomm::Cmd::Get).addr(0).size(1)))
      return 9;
    return exchange(libcomm::Msg::Build(libcomm::Cmd::Terminate)) ? 0 : 10;
  }
  for (int i = 0; i < 4; ++i) {
    std::this_thread::sleep_for(delay);
    if (!exchange(libcomm::Msg::Build(libcomm::Cmd::Get).id(i).addr(0).size(1)))
      return 4;
  }
  std::this_thread::sleep_for(delay);
  const bool waited =
      exchange(libcomm::Msg::Build(libcomm::Cmd::Wait).addr(500));
  const bool terminated =
      exchange(libcomm::Msg::Build(libcomm::Cmd::Terminate));
  return waited && terminated ? 0 : 5;
}
