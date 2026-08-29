/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NANOLIBC_FORMOSA_H
#define NANOLIBC_FORMOSA_H

#include <sys/types.h>

/* Serial / debug CSR addresses — simulation map from formosa_addr_map.h */
#ifndef FORMOSA_SERIAL_BASE
#ifdef UTST
#define FORMOSA_SERIAL_BASE 0x8030
#define FORMOSA_EXIT_BASE 0x8038
#else
/* CP_CTRL page: printbuf @ +0, exit @ +8 (see addr_map/formosa_addr_map.h) */
#define FORMOSA_SERIAL_BASE 0x1000
#define FORMOSA_EXIT_BASE 0x1008
#endif
#endif

void formosa_libc_init();
void formosa_putc(char c);

#endif
