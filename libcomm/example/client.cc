// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <libcomm/libcomm.h>

#include <iostream>
#include <memory>
#include <string>

int main() {
  int id = 0;
  std::string s;
  std::unique_ptr<libcomm::Transceiver> transceiver =
      libcomm::Connect("/tmp/test.socket", nullptr, [&id] {
        std::cout << "<<< Interrupt!! Current id = " << id << " >>>\n";
        id++;
      });

  if (!transceiver) {
    std::cout << "Cannot establish connection" << std::endl;
    return -1;
  }

  transceiver->RegisterSyncHandler(
      [](libcomm::Transceiver *self, const libcomm::Msg &msg) {
        std::cout << "Receiving:\n"
                  << msg << " \"" << reinterpret_cast<const char *>(msg.data())
                  << "\"\n\n";
      });

  for (;;) {
    std::getline(std::cin, s);
    libcomm::Msg msg = libcomm::Msg::Build(libcomm::Cmd::Put)
                           .addr(reinterpret_cast<uint64_t>(s.c_str()))
                           .id(id)
                           .size(s.size())
                           .data(reinterpret_cast<const uint8_t *>(s.c_str()));
    std::cout << "Sending:\n" << msg << " \"" << s << "\"\n\n";

    if (transceiver->Send(msg) < 0) {
      std::cout << "Connection is down!\n";
      break;
    }
  }
  return 0;
}
