/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CP_LOG_H
#define CP_LOG_H

#include <stdio.h>  // IWYU pragma: keep

#ifndef DPRINTF
#ifdef FW_ENABLE_LOG
#define DPRINTF(...) printf(__VA_ARGS__)
#else
#define DPRINTF(...)
#endif
#endif

#endif
