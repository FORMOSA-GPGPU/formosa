/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include <cstdio>

#define CHECK_ERR(expr)                               \
  do {                                                \
    int _err = (expr);                                \
    if (_err != 0) {                                  \
      printf("error at %s:%d\n", __FILE__, __LINE__); \
      return _err;                                    \
    }                                                 \
  } while (0)

#define CHECK_ERR_HANDLE(expr, handle)                \
  do {                                                \
    int _err = (expr);                                \
    if (_err != 0) {                                  \
      handle;                                         \
      printf("error at %s:%d\n", __FILE__, __LINE__); \
      return _err;                                    \
    }                                                 \
  } while (0)

#define CHECK_RET(expr, retval)                       \
  do {                                                \
    int _err = (expr);                                \
    if (_err != 0) {                                  \
      printf("error at %s:%d\n", __FILE__, __LINE__); \
      return (retval);                                \
    }                                                 \
  } while (0)

#define CHECK_RET_HANDLE(expr, retval, handle)        \
  do {                                                \
    int _err = (expr);                                \
    if (_err != 0) {                                  \
      handle;                                         \
      printf("error at %s:%d\n", __FILE__, __LINE__); \
      return (retval);                                \
    }                                                 \
  } while (0)

#define CHECK_NULL(expr)                              \
  do {                                                \
    if ((expr) == nullptr) {                          \
      printf("error at %s:%d\n", __FILE__, __LINE__); \
      return -1;                                      \
    }                                                 \
  } while (0)

#define CHECK_NULL_HANDLE(expr, handle)               \
  do {                                                \
    if ((expr) == nullptr) {                          \
      handle;                                         \
      printf("error at %s:%d\n", __FILE__, __LINE__); \
      return -1;                                      \
    }                                                 \
  } while (0)
