/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdlib.h>

int falloc_init(uintptr_t start, size_t size, size_t alignment, int log);
int falloc_malloc(void **devPtr, size_t size);
int falloc_addr_malloc(uintptr_t addr, size_t size);
int falloc_free(void *devPtr);
void falloc_clean();

int falloc_noncache_init(uintptr_t start, size_t size, size_t alignment,
                         int log);
int falloc_noncache_malloc(void **devPtr, size_t size);
int falloc_noncache_free(void *devPtr);
void falloc_noncache_clean();

#ifdef __cplusplus
}
#endif
