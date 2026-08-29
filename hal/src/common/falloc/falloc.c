/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "falloc.h"

#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

struct Block {
  /* Size of the block. */
  size_t size;
  struct Block *next;
  /* Start address of the data. */
  uintptr_t dptr;
};

typedef struct {
  struct Block free_list;
  struct Block allocated_list;
  size_t alignment;
  FILE *logf;
} FallocCtx;

#define FALLOC_CTX_INITIALIZER                                         \
  {                                                                    \
      .free_list = {.size = (size_t)-1, .next = NULL, .dptr = 0},      \
      .allocated_list = {.size = (size_t)-1, .next = NULL, .dptr = 0}, \
      .alignment = 1,                                                  \
      .logf = NULL,                                                    \
  }

static FallocCtx cache_ctx = FALLOC_CTX_INITIALIZER;
static FallocCtx noncache_ctx = FALLOC_CTX_INITIALIZER;

static void falloc_log_list(FILE *logf, const struct Block *list_head) {
  const struct Block *curr = list_head->next;
  fprintf(logf, "[");
  while (curr) {
    if (curr != list_head->next) {
      fprintf(logf, ",");
    }
    fprintf(logf, "{\"start_addr\": %" PRIuPTR ", \"size\": %zu}\n", curr->dptr,
            curr->size);
    curr = curr->next;
  }
  fprintf(logf, "]");
}

static void falloc_mem_log(FallocCtx *ctx, int op, uintptr_t value) {
  fprintf(ctx->logf, ",{\"operation\": \"%s\",", op ? "malloc" : "free");
  fprintf(ctx->logf, "\"value\": %" PRIuPTR ",", value);
  fprintf(ctx->logf, "\"free_list\":");
  falloc_log_list(ctx->logf, &ctx->free_list);
  fprintf(ctx->logf, ",\"allocated_list\":");
  falloc_log_list(ctx->logf, &ctx->allocated_list);
  fprintf(ctx->logf, "}");
}

static int align_size(const FallocCtx *ctx, size_t n, size_t *aligned) {
  if (n > SIZE_MAX - (ctx->alignment - 1)) {
    return 1;
  }
  *aligned = (n + ctx->alignment - 1) & ~(ctx->alignment - 1);
  return 0;
}

static int falloc_init_impl(FallocCtx *ctx, uintptr_t start, size_t size,
                            size_t alignment, int log, const char *log_path) {
  if (size == 0 || alignment == 0 || (alignment & (alignment - 1)) != 0) {
    return 1;
  }
  struct Block *block = malloc(sizeof(*block));
  if (!block) {
    return 1;
  }

  ctx->alignment = alignment;
  block->size = size;
  block->next = NULL;
  block->dptr = start;
  ctx->free_list.dptr = start;
  ctx->free_list.next = block;

  if (!log) {
    return 0;
  }

  ctx->logf = fopen(log_path, "w");
  if (!ctx->logf) {
    free(block);
    ctx->free_list.next = NULL;
    return 1;
  }
  fprintf(ctx->logf, "[{\"start\": %" PRIuPTR ",\"size\": %zu}", start, size);
  return 0;
}

static int falloc_malloc_impl(FallocCtx *ctx, void **dev_ptr, size_t size) {
  if (dev_ptr == NULL) return 1;
  size_t original_size = size;
  if (align_size(ctx, size, &size) != 0) return 1;
  struct Block *free_block = ctx->free_list.next;
  struct Block *prev = &ctx->free_list;
  while (free_block) {
    if (free_block->size >= size) {
      break;
    }
    prev = free_block;
    free_block = free_block->next;
  }
  if (!free_block) {
    return 1;
  }

  if (free_block->size > size) {
    struct Block *remainder = malloc(sizeof(*remainder));
    if (!remainder) {
      return 1;
    }
    remainder->size = free_block->size - size;
    remainder->next = free_block->next;
    remainder->dptr = free_block->dptr + size;
    free_block->next = remainder;
    free_block->size = size;
  }

  prev->next = free_block->next;
  free_block->next = ctx->allocated_list.next;
  ctx->allocated_list.next = free_block;
  *dev_ptr = (void *)free_block->dptr;
  if (ctx->logf) {
    falloc_mem_log(ctx, 1, original_size);
  }
  return 0;
}

static int falloc_addr_malloc_impl(FallocCtx *ctx, uintptr_t addr,
                                   size_t size) {
  size_t original_size = size;
  if (align_size(ctx, size, &size) != 0) return 1;
  struct Block *free_block = ctx->free_list.next;
  struct Block *prev = &ctx->free_list;
  while (free_block) {
    if (free_block->dptr <= addr &&
        addr < free_block->dptr + free_block->size &&
        addr + size <= free_block->dptr + free_block->size) {
      break;
    }
    prev = free_block;
    free_block = free_block->next;
  }
  if (!free_block) {
    return 1;
  }

  struct Block *right = NULL;
  struct Block *left = NULL;
  if (addr + size < free_block->dptr + free_block->size) {
    right = malloc(sizeof(*right));
    if (!right) {
      return 1;
    }
  }
  if (free_block->dptr < addr) {
    left = malloc(sizeof(*left));
    if (!left) {
      free(right);
      return 1;
    }
  }

  if (right) {
    right->size = free_block->dptr + free_block->size - (addr + size);
    right->next = free_block->next;
    right->dptr = addr + size;
    free_block->next = right;
  }

  if (left) {
    left->size = addr - free_block->dptr;
    prev->next = left;
    left->next = free_block;
    left->dptr = free_block->dptr;
    free_block->dptr = addr;
    prev = left;
  }

  free_block->size = size;
  prev->next = free_block->next;
  free_block->next = ctx->allocated_list.next;
  ctx->allocated_list.next = free_block;
  if (ctx->logf) {
    falloc_mem_log(ctx, 1, original_size);
  }
  return 0;
}

static int falloc_free_impl(FallocCtx *ctx, void *dev_ptr) {
  uintptr_t ptr = (uintptr_t)dev_ptr;
  struct Block *alloc = ctx->allocated_list.next;
  struct Block *alloc_prev = &ctx->allocated_list;
  while (alloc) {
    if (ptr == alloc->dptr) {
      break;
    }
    alloc_prev = alloc;
    alloc = alloc->next;
  }
  if (!alloc) {
    return 1;
  }
  alloc_prev->next = alloc->next;

  struct Block *curr = ctx->free_list.next;
  struct Block *insert = &ctx->free_list;
  while (curr) {
    if (ptr < curr->dptr) {
      break;
    }
    insert = curr;
    curr = curr->next;
  }

  if (insert != &ctx->free_list && insert->dptr + insert->size == alloc->dptr) {
    insert->size += alloc->size;
    free(alloc);
    alloc = insert;
  } else {
    alloc->next = insert->next;
    insert->next = alloc;
  }

  if (alloc->next && alloc->dptr + alloc->size == alloc->next->dptr) {
    struct Block *next = alloc->next;
    alloc->size += next->size;
    alloc->next = next->next;
    free(next);
  }

  if (ctx->logf) {
    falloc_mem_log(ctx, 0, ptr);
  }
  return 0;
}

static void falloc_clean_impl(FallocCtx *ctx) {
  struct Block *curr = ctx->free_list.next;
  while (curr) {
    struct Block *next = curr->next;
    free(curr);
    curr = next;
  }

  curr = ctx->allocated_list.next;
  while (curr) {
    struct Block *next = curr->next;
    free(curr);
    curr = next;
  }

  ctx->free_list = (struct Block){.size = (size_t)-1, .next = NULL, .dptr = 0};
  ctx->allocated_list =
      (struct Block){.size = (size_t)-1, .next = NULL, .dptr = 0};
  ctx->alignment = 1;
  if (ctx->logf) {
    fprintf(ctx->logf, "]");
    fclose(ctx->logf);
    ctx->logf = NULL;
  }
}

int falloc_init(uintptr_t start, size_t size, size_t alignment, int log) {
  return falloc_init_impl(&cache_ctx, start, size, alignment, log, "log.json");
}

int falloc_malloc(void **dev_ptr, size_t size) {
  return falloc_malloc_impl(&cache_ctx, dev_ptr, size);
}

int falloc_addr_malloc(uintptr_t addr, size_t size) {
  return falloc_addr_malloc_impl(&cache_ctx, addr, size);
}

int falloc_free(void *dev_ptr) { return falloc_free_impl(&cache_ctx, dev_ptr); }

void falloc_clean(void) { falloc_clean_impl(&cache_ctx); }

int falloc_noncache_init(uintptr_t start, size_t size, size_t alignment,
                         int log) {
  return falloc_init_impl(&noncache_ctx, start, size, alignment, log,
                          "log_noncache.json");
}

int falloc_noncache_malloc(void **dev_ptr, size_t size) {
  return falloc_malloc_impl(&noncache_ctx, dev_ptr, size);
}

int falloc_noncache_free(void *dev_ptr) {
  return falloc_free_impl(&noncache_ctx, dev_ptr);
}

void falloc_noncache_clean(void) { falloc_clean_impl(&noncache_ctx); }
