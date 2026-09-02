/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CP_PANIC_H
#define CP_PANIC_H

_Noreturn void cp_panic(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

#endif
