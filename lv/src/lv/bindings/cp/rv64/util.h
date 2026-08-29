/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdexcept>
#include <string>

#define likely(x) __builtin_expect((x), 1)
#define unlikely(x) __builtin_expect((x), 0)

#define UNUSED(x) (void)(x)

inline void ensure(bool cond) {
  if (unlikely(!cond)) throw std::runtime_error("runtime assertion failed");
}

inline void ensure(bool cond, const std::string &reason) {
  if (unlikely(!cond)) throw std::runtime_error(reason);
}

#define BIT_RANGE(instr, upper, lower) \
  (instr & (((1 << (upper - lower + 1)) - 1) << lower))
#define BIT_SLICE(instr, upper, lower) (BIT_RANGE(instr, upper, lower) >> lower)
#define BIT_SINGLE(instr, pos) (instr & (1 << pos))
#define BIT_SINGLE_P1(instr, pos) (BIT_SINGLE(instr, pos) >> pos)
#define BIT_SINGLE_PN(instr, pos, new_pos) \
  ((BIT_SINGLE(instr, pos) >> pos) << new_pos)
#define EXTRACT_SIGN_BIT(instr, pos, new_pos) \
  ((BIT_SINGLE_P1(instr, pos) << 31) >> (31 - new_pos))
