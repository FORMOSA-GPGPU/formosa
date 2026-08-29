// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <libcomm/libcomm.h>

#include <iostream>
#include <memory>
#include <utility>
#include <vector>

std::vector<std::unique_ptr<libcomm::Transceiver>> transceivers;

int main() {
  std::unique_ptr<std::thread> accept_thread = libcomm::Serve(
      "/tmp/test.socket", nullptr,
      [](std::unique_ptr<libcomm::Transceiver> transceiver) {
        transceiver->RegisterSyncHandler([](libcomm::Transceiver *self,
                                            const libcomm::Msg &msg) {
          std::cout << "---" << std::endl;
          std::cout << "Message from: " << static_cast<void *>(self) << ":\n"
                    << msg << " \"";
          std::cout << reinterpret_cast<const char *>(msg.data()) << "\"\n\n";

          libcomm::Msg resp = libcomm::Msg::Respond(msg);
          std::cout << "Replying:\n" << resp << "\n\n";
          self->Send(resp);

          std::cout << "Sending interrupt" << std::endl;
          self->Send();
          std::cout << "Done!\n";
          std::cout << "---" << std::endl;
        });
        transceivers.push_back(std::move(transceiver));
      });

  if (!accept_thread) {
    std::cout << "Cannot open the server" << std::endl;
    return -1;
  }

  // User can use Ctrl-Z to peek all transceivers' status.
  std::signal(SIGTSTP, [](int) {
    std::cout << "\nTransceivers:\n";
    bool all_dead = true;
    for (auto &t : transceivers) {
      std::cout << &t << ": alive = " << t->IsConnectionAlive() << std::endl;
      all_dead &= !t->IsConnectionAlive();
    }

    if (all_dead) {
      std::cout << "All connections are dead, exiting..." << std::endl;
      exit(0);
    }
  });

  accept_thread->join();
  return 0;
}
