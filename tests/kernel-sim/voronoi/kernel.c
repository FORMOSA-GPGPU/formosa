/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "kernel.h"

#include <limits.h>

typedef struct {
  int x, y;
  int color;  // in 0xRRGGBB format
} seed_t;

typedef struct {
  int *output_image;
  seed_t *seeds;
  int num_seeds;
  int width;
  int height;
} kargs_t;

void voronoi(int *output_image, seed_t *seeds, int num_seeds, int width,
             int height) {
  int tid = global_id();
  if (tid >= width * height) return;

  // Turn tid into 2D coordinates (px, py)
  int py = tid / width;
  int px = tid % width;

  int min_dist = INT_MAX;
  int closest_seed_color = 0;

  // Find the closest seed point
  for (int i = 0; i < num_seeds; i++) {
    // Calculate distance: (x1 - x2)^2 + (y1 - y2)^2
    int dx = px - seeds[i].x;
    int dy = py - seeds[i].y;
    int dist = dx * dx + dy * dy;

    if (dist == 0) {
      closest_seed_color = 0;
      break;
    }

    if (dist < min_dist) {
      min_dist = dist;
      closest_seed_color = seeds[i].color;
    }
  }

  // Assign the color of the closest seed to the pixel
  output_image[tid] = closest_seed_color;
}

void kernel(void *args) {
  kargs_t k = *(kargs_t *)args;
  voronoi(k.output_image, k.seeds, k.num_seeds, k.width, k.height);
}
