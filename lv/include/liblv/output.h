/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

// Backward-compatibility shim. The output/logging API now lives in
// <liblv/log.h>, which defines lv::fatal_error and the lv::Info/Warning/
// Fatal/Error/Debug/Trace/Print free functions. Existing includers keep
// working unchanged; prefer including <liblv/log.h> directly in new code.
#include <liblv/log.h>
