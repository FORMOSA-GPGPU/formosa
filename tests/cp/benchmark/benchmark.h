/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 * SPDX-License-Identifier: Apache-2.0 */

#ifndef CP_BENCHMARK_H
#define CP_BENCHMARK_H

/* Test-local RAM markers, observed by cp.benchmark.check at one-cycle
 * resolution. */
void benchmark_start(void);
void benchmark_stop(void);
void benchmark_finish(unsigned iterations, unsigned errors)
    __attribute__((noreturn));

#endif
