// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/time.h>

namespace {

const auto &start_time() {
  static const auto t = std::chrono::steady_clock::now();
  return t;
}

}  // namespace

namespace lv {

void initialize_elapsed_time() { (void)start_time(); }

std::chrono::steady_clock::duration elapsed_duration() {
  return std::chrono::steady_clock::now() - start_time();
}

std::chrono::seconds elapsed_time() {
  return std::chrono::duration_cast<std::chrono::seconds>(elapsed_duration());
}

}  // namespace lv
