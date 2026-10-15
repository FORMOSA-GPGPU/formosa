// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

// Run the independent upstream CPU model and solver with its original inputs.
#include <math.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>

// The upstream sources are included in dependency order, as in its CPU main.c.
// clang-format off
#include "openmp/myocyte/define.c"
#include "openmp/myocyte/ecc.c"
#include "openmp/myocyte/cam.c"
#include "openmp/myocyte/fin.c"
#include "openmp/myocyte/master.c"
#include "openmp/myocyte/embedded_fehlberg_7_8.c"
#include "openmp/myocyte/solver.c"
// clang-format on

static int read_values(const char *path, float *values, int count) {
  FILE *file = fopen(path, "r");
  if (!file) return 0;
  for (int i = 0; i < count; ++i) {
    if (fscanf(file, "%f", &values[i]) != 1) {
      fclose(file);
      return 0;
    }
  }
  return fclose(file) == 0;
}

int main(int argc, char **argv) {
  if (argc != 4) return 1;
  float initial[EQUATIONS], final[EQUATIONS], params[PARAMETERS], time[2];
  float *values[] = {initial, final};
  if (!read_values(argv[1], initial, EQUATIONS) ||
      !read_values(argv[2], params, PARAMETERS))
    return 1;
  if (solver(values, time, 1, params, 1)) return 1;
  FILE *file = fopen(argv[3], "w");
  if (!file) return 1;
  for (int step = 0; step < 2; ++step)
    for (int i = 0; i < EQUATIONS; ++i)
      fprintf(file, "y[0][%d][%d]=%.9g\n", step, i, values[step][i]);
  return fclose(file) != 0;
}
