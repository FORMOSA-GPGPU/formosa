# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

include(CheckCXXCompilerFlag)
include(CheckCXXSourceRuns)

# Check avx-512f
set(AVX512F_FLAG "-mavx512f")
check_cxx_compiler_flag("${AVX512F_FLAG}" COMPILER_SUPPORTS_AVX512F)

if(COMPILER_SUPPORTS_AVX512F)
  message(STATUS "Check if host supports AVX-512 Foundation")
  set(CMAKE_REQUIRED_FLAGS "${AVX512F_FLAG}")
  check_cxx_source_runs("
      #include <immintrin.h>
      int main(int argc, char** argv) {
          __m512i a = _mm512_set1_epi64(argc);
          __m512i b = _mm512_set1_epi64(2);
          __m512i c = _mm512_add_epi64(a, b);
          alignas(64) long long res[8];
          _mm512_store_si512((__m512i*)res, c);
          return (res[0] == (argc + 2)) ? 0 : 1;
      }
  " HOST_HAS_AVX512F)
  unset(CMAKE_REQUIRED_FLAGS)
endif()

# Check avx-512vl
# VL requires Foundation
set(AVX512VL_FLAG "-mavx512f -mavx512vl")
check_cxx_compiler_flag("${AVX512VL_FLAG}" COMPILER_SUPPORTS_AVX512VL)

if(COMPILER_SUPPORTS_AVX512VL)
  message(STATUS "Check if host supports AVX-512 VL")
  set(CMAKE_REQUIRED_FLAGS "${AVX512VL_FLAG}")
  check_cxx_source_runs("
      #include <immintrin.h>
      int main(int argc, char** argv) {
          /* VL allows AVX512 features (masking) on YMM (256-bit) registers */
          __m256i a = _mm256_set1_epi32(argc);
          __m256i b = _mm256_set1_epi32(20);
          /* _mm256_mask_add_epi32 requires AVX512VL */
          __m256i res = _mm256_mask_add_epi32(a, 0xAA, a, b);
          alignas(32) int out[8];
          _mm256_store_si256((__m256i*)out, res);
          return (out[0] == argc) ? 0 : 1;
      }
  " HOST_HAS_AVX512VL)
  unset(CMAKE_REQUIRED_FLAGS)
endif()

# Check avx-512dq
# DQ requires Foundation
set(AVX512DQ_FLAG "-mavx512f -mavx512dq -mavx512vl")
check_cxx_compiler_flag("${AVX512DQ_FLAG}" COMPILER_SUPPORTS_AVX512DQ)

if(COMPILER_SUPPORTS_AVX512DQ)
  message(STATUS "Check if host supports AVX-512 DQ")
  set(CMAKE_REQUIRED_FLAGS "${AVX512DQ_FLAG}")
  check_cxx_source_runs("
      #include <immintrin.h>
      int main(int argc, char** argv) {
          /* DQ adds 64-bit integer multiply on YMM/ZMM registers */
          __m256i a = _mm256_set1_epi64x(argc);
          __m256i b = _mm256_set1_epi64x(20);
          __m256i res = _mm256_mullo_epi64(a, b);
          alignas(32) long long out[4];
          _mm256_store_si256((__m256i*)out, res);
          return (out[0] == (argc * 20)) ? 0 : 1;
      }
  " HOST_HAS_AVX512DQ)
  unset(CMAKE_REQUIRED_FLAGS)
endif()

message(STATUS "AVX-512 Foundation Available: ${HOST_HAS_AVX512F}")
message(STATUS "AVX-512 VL Available:         ${HOST_HAS_AVX512VL}")
message(STATUS "AVX-512 DQ Available:         ${HOST_HAS_AVX512DQ}")

if(HOST_HAS_AVX512F AND HOST_HAS_AVX512VL AND HOST_HAS_AVX512DQ)
  set(HOST_HAS_AVX512_SIMD TRUE)
else()
  set(HOST_HAS_AVX512_SIMD FALSE)
endif()

message(STATUS "AVX-512 SIMD support:         ${HOST_HAS_AVX512_SIMD}")
