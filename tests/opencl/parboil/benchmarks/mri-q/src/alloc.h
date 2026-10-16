/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MRIQ_ALLOC_H
#define MRIQ_ALLOC_H

#include <stdlib.h>

/* Keep the CPU and OpenCL host buffers aligned to the original 16 bytes. */
static inline void *mriq_alloc(size_t size)
{
  void *ptr = NULL;
  if (posix_memalign(&ptr, 16, size) != 0) return NULL;
  return ptr;
}

#endif
