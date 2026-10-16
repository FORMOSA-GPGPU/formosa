/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef ENABLE_THREADS
#include <pthread.h>
using StreamclusterBarrier = pthread_barrier_t;
#else
// Both streamcluster hosts pass this context through their serial path.
struct StreamclusterBarrier {};
#endif
