// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
// SPDX-License-Identifier: Apache-2.0

#include <libcomm/libcomm.h>

#include <cassert>
#include <cstdint>

int main() {
  const std::uint8_t payload[] = {0x12, 0x34, 0x56, 0x78};
  const libcomm::Msg request = libcomm::Msg::Build(libcomm::Cmd::Put)
                                   .id(7)
                                   .addr(0x1000)
                                   .size(sizeof(payload))
                                   .data(payload);

  assert(request.is_request());
  assert(request.has_data());
  assert(request.cmd() == libcomm::Cmd::Put);
  assert(request.id() == 7);
  assert(request.addr() == 0x1000);
  assert(request.size() == sizeof(payload));

  const libcomm::Msg response =
      libcomm::Msg::Respond(request).status(libcomm::Status::Okay);
  assert(response.is_response());
  assert(response.status() == libcomm::Status::Okay);
  assert(response.id() == request.id());
  assert(response.addr() == request.addr());
  assert(response.size() == request.size());
  assert(response.data() == request.data());

  return 0;
}
