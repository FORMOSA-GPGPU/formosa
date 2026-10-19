/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 * SPDX-License-Identifier: Apache-2.0 */

/**
 * @file
 * @brief Shared RISC-V PLIC 1.0.0 driver using little-endian 32-bit MMIO.
 *
 * The BSP supplies the mapped base address, implemented source/context IDs,
 * and supported WARL priority values. This driver does not validate arguments,
 * discover the platform layout, or configure CPU interrupt CSRs and traps.
 * All MMIO accesses are fenced; device-specific cache maintenance remains the
 * caller's responsibility.
 */

#ifndef LV_DRIVERS_SIMPLE_PLIC_H_
#define LV_DRIVERS_SIMPLE_PLIC_H_

#include <lv/mmio/simple/plic.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Order surrounding device I/O and memory accesses.
 *
 * Volatile performs the access; fences also order surrounding I/O and memory.
 * The memory clobber prevents compiler reordering across the same boundary.
 * This barrier does not perform cache maintenance or make RMW updates atomic.
 */
static inline void lv_plic_detail_fence(void) {
  __asm__ volatile("fence iorw, iorw" ::: "memory");
}

/**
 * @brief Read one volatile MMIO word with fences before and after the access.
 * @param address Mapped, 32-bit-aligned register address.
 * @return The value read from the register.
 * @note The read may trigger register side effects, such as interrupt claim.
 */
static inline uint32_t lv_plic_detail_read32(uintptr_t address) {
  lv_plic_detail_fence();
  const uint32_t value = *(volatile uint32_t *)address;
  lv_plic_detail_fence();
  return value;
}

/**
 * @brief Write one volatile MMIO word with fences before and after the access.
 * @param address Mapped, 32-bit-aligned register address.
 * @param value Full 32-bit register value to write.
 */
static inline void lv_plic_detail_write32(uintptr_t address, uint32_t value) {
  lv_plic_detail_fence();
  *(volatile uint32_t *)address = value;
  lv_plic_detail_fence();
}

/**
 * @brief Locate the enable bitmap word containing a source's bit.
 * @param base Mapped PLIC base address.
 * @param context Implemented zero-based context ID.
 * @param source Implemented nonzero source ID.
 * @return Address of the word containing bit @c source%32.
 */
static inline uintptr_t lv_plic_detail_enable_word_address(uintptr_t base,
                                                           uint32_t context,
                                                           uint32_t source) {
  return base + LV_PLIC_ENABLE_BASE +
         (uintptr_t)context * LV_PLIC_ENABLE_STRIDE +
         (source / 32) * LV_PLIC_REGISTER_BYTES;
}

/**
 * @brief Locate a context's threshold and claim/complete register block.
 * @param base Mapped PLIC base address.
 * @param context Implemented zero-based context ID.
 * @return Address of the context's threshold register.
 */
static inline uintptr_t lv_plic_detail_context_address(uintptr_t base,
                                                       uint32_t context) {
  return base + LV_PLIC_CONTEXT_BASE +
         (uintptr_t)context * LV_PLIC_CONTEXT_STRIDE;
}

/**
 * @brief Set the priority shared by all contexts for an interrupt source.
 *
 * @param base Mapped, 32-bit-aligned PLIC base address.
 * @param source Implemented source ID in 1..1023.
 * @param priority Platform-supported WARL priority; 0 disables arbitration
 *                 for this source without clearing its pending bit.
 */
static inline void plic_set_priority(uintptr_t base, uint32_t source,
                                     uint32_t priority) {
  lv_plic_detail_write32(base + (uintptr_t)source * LV_PLIC_REGISTER_BYTES,
                         priority);
}

/**
 * @brief Enable a source for one interrupt context.
 *
 * @param base Mapped, 32-bit-aligned PLIC base address.
 * @param context Implemented zero-based context ID in 0..15871.
 * @param source Implemented source ID in 1..1023.
 * @pre The caller serializes updates to the same context enable word,
 *      including updates from interrupt handlers. No lock is provided.
 */
static inline void plic_enable(uintptr_t base, uint32_t context,
                               uint32_t source) {
  const uintptr_t address =
      lv_plic_detail_enable_word_address(base, context, source);
  lv_plic_detail_write32(
      address, lv_plic_detail_read32(address) | (UINT32_C(1) << (source % 32)));
}

/**
 * @brief Disable a source for one context without clearing its pending bit.
 *
 * @param base Mapped, 32-bit-aligned PLIC base address.
 * @param context Implemented zero-based context ID in 0..15871.
 * @param source Implemented source ID in 1..1023.
 * @pre The caller serializes updates to the same context enable word,
 *      including updates from interrupt handlers. No lock is provided.
 */
static inline void plic_disable(uintptr_t base, uint32_t context,
                                uint32_t source) {
  const uintptr_t address =
      lv_plic_detail_enable_word_address(base, context, source);
  lv_plic_detail_write32(address, lv_plic_detail_read32(address) &
                                      ~(UINT32_C(1) << (source % 32)));
}

/**
 * @brief Set the priority threshold for a context's IRQ notifications.
 *
 * @param base Mapped, 32-bit-aligned PLIC base address.
 * @param context Implemented zero-based context ID in 0..15871.
 * @param threshold Platform-supported WARL threshold. Notification requires
 *                  a source priority strictly greater than this value.
 * @note The threshold does not restrict plic_claim().
 */
static inline void plic_set_threshold(uintptr_t base, uint32_t context,
                                      uint32_t threshold) {
  lv_plic_detail_write32(
      lv_plic_detail_context_address(base, context) + LV_PLIC_THRESHOLD_OFFSET,
      threshold);
}

/**
 * @brief Claim the highest-priority enabled pending source for a context.
 *
 * @param base Mapped, 32-bit-aligned PLIC base address.
 * @param context Implemented zero-based context ID in 0..15871.
 * @return The claimed source ID, or 0 if no enabled, nonzero-priority source
 *         is pending. Equal priorities are resolved by the lowest source ID.
 *
 * A successful claim atomically clears the source's pending bit. Its gateway
 * remains busy until plic_complete(). Claim ignores the context threshold and
 * may be used for polling even when no IRQ notification is asserted.
 */
static inline uint32_t plic_claim(uintptr_t base, uint32_t context) {
  return lv_plic_detail_read32(lv_plic_detail_context_address(base, context) +
                               LV_PLIC_CLAIM_COMPLETE_OFFSET);
}

/**
 * @brief Complete service and allow the source gateway to request again.
 *
 * @param base Mapped, 32-bit-aligned PLIC base address.
 * @param context Implemented zero-based context ID in 0..15871.
 * @param source Nonzero source ID returned by plic_claim(); never complete 0.
 * @pre Acknowledge the interrupt at its device before completing it here.
 * @pre Keep the source enabled for this context until completion is written.
 * @note A level-sensitive source that remains asserted can become pending
 *       again after completion.
 */
static inline void plic_complete(uintptr_t base, uint32_t context,
                                 uint32_t source) {
  lv_plic_detail_write32(lv_plic_detail_context_address(base, context) +
                             LV_PLIC_CLAIM_COMPLETE_OFFSET,
                         source);
}

#ifdef __cplusplus
}
#endif

#endif /* LV_DRIVERS_SIMPLE_PLIC_H_ */
