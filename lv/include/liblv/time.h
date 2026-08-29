/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <chrono>

namespace lv {

void initialize_elapsed_time();
std::chrono::steady_clock::duration elapsed_duration();
std::chrono::seconds elapsed_time();

}  // namespace lv
