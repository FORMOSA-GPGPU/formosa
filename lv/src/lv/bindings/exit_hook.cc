// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>

#include <csignal>
#include <cstdlib>
#include <utility>

namespace {
sol::function &ExitHook() {
  static sol::function hook;
  return hook;
}

void handle_exit_signal(int) {
  if (ExitHook().valid()) ExitHook()();
  // Global teardown is unsafe from an asynchronous signal handler.
  std::_Exit(0);
}
}  // namespace

LV_MODULE(lv)
    .init([] {
      // Destroy the callback before the Lua runtime created by ModuleBuilder.
      ExitHook();
      std::signal(SIGINT, handle_exit_signal);
      std::signal(SIGTERM, handle_exit_signal);
    })
    .function(
        "exit_hook",
        [](sol::function f) {
          ExitHook() = std::move(f);
        },
        lv::params(lv::param("f")),
        lv::doc("Register a Lua callback invoked on SIGINT or SIGTERM."));
