/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 * SPDX-License-Identifier: Apache-2.0 */

#ifndef LV_MMIO_SIMPLE_PFREADER_H_
#define LV_MMIO_SIMPLE_PFREADER_H_

/* PerfettoReader: track n occupies the 8-byte slot at base + n*stride.
 * Software writes a signed value in the low 32 bits of a slot, normally with
 * a 64-bit store. Slice/instant values index the configured content names;
 * a negative slice value ends the slice. Counter values are signed 32-bit.
 * The platform supplies the base address, track order, types and names.
 */
#define LV_PFREADER_TRACK_STRIDE 8
#define LV_PFREADER_SLICE_END (-1)

#endif /* LV_MMIO_SIMPLE_PFREADER_H_ */
