/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 * SPDX-License-Identifier: Apache-2.0
 */
#include "benchmark.h"
#include "coremark.h"

#if !defined(ITERATIONS) || ITERATIONS != 10
#error "The final CRC references require ITERATIONS=10"
#endif

#if VALIDATION_RUN
#define INPUT_SEED 0x3415
#define LIST_CRC 0xe3c1
#define MATRIX_CRC 0x0747
#define STATE_CRC 0x8d84
#define FINAL_CRC 0xc64e
#else
#define INPUT_SEED 0
#define LIST_CRC 0xe714
#define MATRIX_CRC 0x1fd7
#define STATE_CRC 0x8e3a
#define FINAL_CRC 0xfcaf
#endif

volatile ee_s32 seed1_volatile = INPUT_SEED;
volatile ee_s32 seed2_volatile = INPUT_SEED;
volatile ee_s32 seed3_volatile = 0x66;
volatile ee_s32 seed4_volatile = ITERATIONS;
volatile ee_s32 seed5_volatile = 0;
ee_u32 default_num_contexts = 1;

void start_time(void) { benchmark_start(); }
void stop_time(void) { benchmark_stop(); }

/* The Lua harness measures the markers in simulation cycles. No target timer
 * is available, so upstream's seconds/rate reporting is deliberately unused.
 * These zeroes also prevent upstream from claiming a valid >=10-second run.
 */
CORE_TICKS get_time(void) { return 0; }
secs_ret time_in_secs(CORE_TICKS ticks) {
  (void)ticks;
  return 0;
}

void portable_init(core_portable *p, int *argc, char *argv[]) {
  (void)argc;
  (void)argv;
  p->portable_id = 1;
}

void portable_fini(core_portable *p) {
  core_results *r = (core_results *)((char *)p - offsetof(core_results, port));
  unsigned errors = check_data_types();
  errors +=
      r->seed1 != INPUT_SEED || r->seed2 != INPUT_SEED || r->seed3 != 0x66;
  errors += r->size != 2000 / NUM_ALGORITHMS;
  errors += r->execs != ALL_ALGORITHMS_MASK || r->iterations != ITERATIONS;
  errors += r->crclist != LIST_CRC;
  errors += r->crcmatrix != MATRIX_CRC;
  errors += r->crcstate != STATE_CRC;
  errors += r->crc != FINAL_CRC;
  errors += r->err != 0;
  p->portable_id = 0;
  benchmark_finish(r->iterations, errors);
}
