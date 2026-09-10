// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>

#include <csignal>
#include <cstdlib>

namespace {
sol::function exit_hook;

void handle_exit_signal(int) {
  if (exit_hook.valid()) exit_hook();
  // Global teardown is unsafe from an asynchronous signal handler.
  std::_Exit(0);
}
}  // namespace

LV_MODULE(lv)
    .init([] {
      std::signal(SIGINT, handle_exit_signal);
      std::signal(SIGTERM, handle_exit_signal);
    })
    .function(
        "exit_hook",
        [](sol::function f) {
          exit_hook = f;
        },
        lv::params(lv::param("f")),
        lv::doc("Register a Lua callback invoked on SIGINT or SIGTERM."));
