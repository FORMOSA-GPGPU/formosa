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

#ifndef LV_DRIVERS_PLIC_H_
#define LV_DRIVERS_PLIC_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Set the priority shared by all contexts for an interrupt source.
 *
 * @param base Mapped, 32-bit-aligned PLIC base address.
 * @param source Implemented source ID in 1..1023.
 * @param priority Platform-supported WARL priority; 0 disables arbitration
 *                 for this source without clearing its pending bit.
 */
void plic_set_priority(uintptr_t base, uint32_t source, uint32_t priority);

/**
 * @brief Enable a source for one interrupt context.
 *
 * @param base Mapped, 32-bit-aligned PLIC base address.
 * @param context Implemented zero-based context ID in 0..15871.
 * @param source Implemented source ID in 1..1023.
 * @pre The caller serializes updates to the same context enable word,
 *      including updates from interrupt handlers. No lock is provided.
 */
void plic_enable(uintptr_t base, uint32_t context, uint32_t source);

/**
 * @brief Disable a source for one context without clearing its pending bit.
 *
 * @param base Mapped, 32-bit-aligned PLIC base address.
 * @param context Implemented zero-based context ID in 0..15871.
 * @param source Implemented source ID in 1..1023.
 * @pre The caller serializes updates to the same context enable word,
 *      including updates from interrupt handlers. No lock is provided.
 */
void plic_disable(uintptr_t base, uint32_t context, uint32_t source);

/**
 * @brief Set the priority threshold for a context's IRQ notifications.
 *
 * @param base Mapped, 32-bit-aligned PLIC base address.
 * @param context Implemented zero-based context ID in 0..15871.
 * @param threshold Platform-supported WARL threshold. Notification requires
 *                  a source priority strictly greater than this value.
 * @note The threshold does not restrict plic_claim().
 */
void plic_set_threshold(uintptr_t base, uint32_t context, uint32_t threshold);

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
uint32_t plic_claim(uintptr_t base, uint32_t context);

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
void plic_complete(uintptr_t base, uint32_t context, uint32_t source);

#ifdef __cplusplus
}
#endif

#endif /* LV_DRIVERS_PLIC_H_ */
