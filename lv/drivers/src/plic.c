/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 * SPDX-License-Identifier: Apache-2.0 */

/**
 * @file
 * @brief Fenced MMIO implementation of the shared PLIC driver.
 *
 * Compile this source with the consumer's RISC-V toolchain and ISA/ABI flags.
 * Register offsets are relative to the caller-supplied PLIC base address.
 */

#include <plic.h>

/** @brief Fixed PLIC register offsets and per-context strides, in bytes. */
enum {
  PLIC_ENABLE_BASE = 0x2000,
  PLIC_ENABLE_STRIDE = 0x80,
  PLIC_CONTEXT_BASE = 0x200000,
  PLIC_CONTEXT_STRIDE = 0x1000,
  PLIC_CLAIM_COMPLETE_OFFSET = 4,
};

/**
 * @brief Order surrounding device I/O and memory accesses.
 *
 * Volatile performs the access; fences also order surrounding I/O and memory.
 * The memory clobber prevents compiler reordering across the same boundary.
 * This barrier does not perform cache maintenance or make RMW updates atomic.
 */
static inline void mmio_fence(void) {
  __asm__ volatile("fence iorw, iorw" ::: "memory");
}

/**
 * @brief Read one volatile MMIO word with fences before and after the access.
 * @param address Mapped, 32-bit-aligned register address.
 * @return The value read from the register.
 * @note The read may trigger register side effects, such as interrupt claim.
 */
static inline uint32_t read32(uintptr_t address) {
  mmio_fence();
  const uint32_t value = *(volatile uint32_t *)address;
  mmio_fence();
  return value;
}

/**
 * @brief Write one volatile MMIO word with fences before and after the access.
 * @param address Mapped, 32-bit-aligned register address.
 * @param value Full 32-bit register value to write.
 */
static inline void write32(uintptr_t address, uint32_t value) {
  mmio_fence();
  *(volatile uint32_t *)address = value;
  mmio_fence();
}

/**
 * @brief Locate the enable bitmap word containing a source's bit.
 * @param base Mapped PLIC base address.
 * @param context Implemented zero-based context ID.
 * @param source Implemented nonzero source ID.
 * @return Address of the word containing bit @c source%32.
 */
static inline uintptr_t enable_word_address(uintptr_t base, uint32_t context,
                                            uint32_t source) {
  return base + PLIC_ENABLE_BASE + (uintptr_t)context * PLIC_ENABLE_STRIDE +
         (source / 32) * 4;
}

/**
 * @brief Locate a context's threshold and claim/complete register block.
 * @param base Mapped PLIC base address.
 * @param context Implemented zero-based context ID.
 * @return Address of the context's threshold register.
 */
static inline uintptr_t context_address(uintptr_t base, uint32_t context) {
  return base + PLIC_CONTEXT_BASE + (uintptr_t)context * PLIC_CONTEXT_STRIDE;
}

/** @details Writes the requested value directly; hardware applies WARL rules.
 */
void plic_set_priority(uintptr_t base, uint32_t source, uint32_t priority) {
  write32(base + (uintptr_t)source * 4, priority);
}

/** @details Uses a fenced read-modify-write to preserve other enable bits. */
void plic_enable(uintptr_t base, uint32_t context, uint32_t source) {
  const uintptr_t address = enable_word_address(base, context, source);
  write32(address, read32(address) | (UINT32_C(1) << (source % 32)));
}

/** @details Uses a fenced read-modify-write to preserve other enable bits. */
void plic_disable(uintptr_t base, uint32_t context, uint32_t source) {
  const uintptr_t address = enable_word_address(base, context, source);
  write32(address, read32(address) & ~(UINT32_C(1) << (source % 32)));
}

/** @details Writes the requested value directly; hardware applies WARL rules.
 */
void plic_set_threshold(uintptr_t base, uint32_t context, uint32_t threshold) {
  write32(context_address(base, context), threshold);
}

/** @details Issues exactly one side-effecting read of the claim register. */
uint32_t plic_claim(uintptr_t base, uint32_t context) {
  return read32(context_address(base, context) + PLIC_CLAIM_COMPLETE_OFFSET);
}

/** @details Writes the serviced ID to the same offset used for claim reads. */
void plic_complete(uintptr_t base, uint32_t context, uint32_t source) {
  write32(context_address(base, context) + PLIC_CLAIM_COMPLETE_OFFSET, source);
}
