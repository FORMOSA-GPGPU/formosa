/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CP_DISPATCH_H
#define CP_DISPATCH_H

#include <stdbool.h>

void cp_dispatch(void *args);

bool cp_has_active_kernel_or_cache(void);

#endif
