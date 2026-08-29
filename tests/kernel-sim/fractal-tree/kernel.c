/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "kernel.h"

typedef struct {
  int x1, y1;
  int x2, y2;
} branch_t;

typedef struct {
  branch_t *branches;
  int *SIN_TABLE;
  int *COS_TABLE;
  int table_size;
  int SHIFT;

  int total_branches;
  int initial_len;
  int L_angle;
  int R_angle;
  int L_shrink;
  int R_shrink;
} kargs_t;

void fractal_tree(branch_t *branches, int *SIN_TABLE, int *COS_TABLE,
                  int table_size, int SHIFT, int total_branches,
                  int initial_len, int L_angle, int R_angle, int L_shrink,
                  int R_shrink) {
  int tid = global_id();
  if (tid < 0 || tid >= total_branches) return;

  unsigned int node = (unsigned int)(tid + 1);

  // Calculate depth: Root is at depth 0
  int depth = 0;
  unsigned int t = node >> 1;
  while (t) {
    depth++;
    t >>= 1;
  }

  // Starting point and angle (upwards)
  int x = 0, y = 0;
  int angle = 90;
  int len = initial_len << SHIFT;

  // Walk from root to this node
  for (int d = depth - 1; d >= 0; --d) {
    // Move one step in the current direction
    x += ((COS_TABLE[angle] * len) >> SHIFT);
    y += ((SIN_TABLE[angle] * len) >> SHIFT);

    // Determine direction at this depth (0=left, 1=right)
    // If tid=12 --> node=13 (binary 1101), depth=3:
    //  1   1   0   1
    //      |   |   ^~ moving from depth=2 to depth=3 (go left)
    //      |   |
    //      |   |~ moving from depth=1 to depth=2 (go right)
    //      |
    //      |~ direction when moving from depth=0 to depth=1 (go right)
    int dir = (node >> d) & 1;

    // Update angle and length based on direction
    if (dir == 0) {
      angle = (angle + L_angle) % table_size;
      len = (len * L_shrink) >> SHIFT;
    } else {
      angle = (angle - R_angle + table_size) % table_size;
      len = (len * R_shrink) >> SHIFT;
    }
  }

  // End point of this branch (one more step with current state)
  int x2 = x + ((COS_TABLE[angle] * len) >> SHIFT);
  int y2 = y + ((SIN_TABLE[angle] * len) >> SHIFT);

  // Store in pixel coordinates
  branches[tid].x1 = x >> SHIFT;
  branches[tid].y1 = y >> SHIFT;
  branches[tid].x2 = x2 >> SHIFT;
  branches[tid].y2 = y2 >> SHIFT;
}

void kernel(void *args) {
  kargs_t k = *(kargs_t *)args;
  fractal_tree(k.branches, k.SIN_TABLE, k.COS_TABLE, k.table_size, k.SHIFT,
               k.total_branches, k.initial_len, k.L_angle, k.R_angle,
               k.L_shrink, k.R_shrink);
}
