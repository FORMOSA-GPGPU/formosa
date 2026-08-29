/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <type_traits>

extern "C" {
#include <softfloat.h>
}

namespace simtix {

/* traits for floating point types to abstract SoftFloat specifics */
template <typename T>
struct fp_traits;

template <>
struct fp_traits<float32_t> {
  using bits_t = uint32_t;
  static constexpr bits_t sign_mask = 0x80000000;
  static constexpr bits_t default_nan = 0x7FC00000;

  static inline bool is_nan(bits_t a) {
    return (((~a) & 0x7F800000) == 0) && (a & 0x007FFFFF);
  }

  static inline bool is_snan(bits_t a) {
    return (((a) & 0x7FC00000) == 0x7F800000) && ((a) & 0x003FFFFF);
  }

  static inline bits_t frac(bits_t a) { return (a) & 0x007FFFFF; }
  static inline bits_t exp(bits_t a) { return ((a) >> 23) & 0xFF; }
  static inline bool sign(bits_t a) { return static_cast<bool>(a >> 31); }
};

template <>
struct fp_traits<float64_t> {
  using bits_t = uint64_t;
  static constexpr bits_t sign_mask = UINT64_C(0x8000000000000000);
  static constexpr bits_t default_nan = UINT64_C(0x7FF8000000000000);

  static inline bool is_nan(bits_t a) {
    return (((~a) & UINT64_C(0x7FF0000000000000)) == 0) &&
           (a & UINT64_C(0x000FFFFFFFFFFFFF));
  }

  static inline bool is_snan(bits_t a) {
    return (
        (((a)&UINT64_C(0x7FF8000000000000)) == UINT64_C(0x7FF0000000000000)) &&
        ((a)&UINT64_C(0x0007FFFFFFFFFFFF)));
  }

  static inline bits_t frac(bits_t a) {
    return a & UINT64_C(0x000FFFFFFFFFFFFF);
  }
  static inline bits_t exp(bits_t a) { return ((a) >> 52) & 0x7FF; }
  static inline bool sign(bits_t a) { return static_cast<bool>(a >> 63); }
};

/* bitwise conversion helpers */
template <typename T>
inline T unbox(int64_t val) {
  return {static_cast<typename fp_traits<T>::bits_t>(val)};
}

template <typename T>
inline int64_t box(T val) {
  /* sign extend 32-bit floats to 64-bit for Zdinx/Zfinx register parity */
  if constexpr (std::is_same_v<T, float32_t>) {
    return static_cast<int32_t>(val.v);
  }
  return static_cast<int64_t>(val.v);
}

inline void softfloat_init(uint32_t rm) {
  softfloat_exceptionFlags = 0;
  softfloat_roundingMode = rm;
}

}  // namespace simtix
