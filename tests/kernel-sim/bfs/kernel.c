/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "kernel.h"

typedef struct {
  int starting;
  int no_of_edges;
} node_t;

typedef struct {
  node_t *graph_nodes;
  int *graph_edges;
  int *graph_mask;
  int *updating_graph_mask;
  int *graph_visited;
  int *cost;
  int *over;
  int no_of_nodes;
  int phase;
} kargs_t;

static void bfs_expand_phase(const kargs_t *kargs, int tid) {
  if (tid >= kargs->no_of_nodes || !kargs->graph_mask[tid]) {
    return;
  }

  kargs->graph_mask[tid] = 0;
  int start = kargs->graph_nodes[tid].starting;
  int end = start + kargs->graph_nodes[tid].no_of_edges;
  for (int edge_idx = start; edge_idx < end; ++edge_idx) {
    int id = kargs->graph_edges[edge_idx];
    if (!kargs->graph_visited[id]) {
      kargs->cost[id] = kargs->cost[tid] + 1;
      kargs->updating_graph_mask[id] = 1;
    }
  }
}

static void bfs_commit_phase(const kargs_t *kargs, int tid) {
  if (tid >= kargs->no_of_nodes || !kargs->updating_graph_mask[tid]) {
    return;
  }

  kargs->graph_mask[tid] = 1;
  kargs->graph_visited[tid] = 1;
  kargs->updating_graph_mask[tid] = 0;
  *(kargs->over) = 1;
}

void kernel(void *args) {
  kargs_t kargs = *(kargs_t *)args;
  int tid = global_id();
  if (kargs.phase == 0) {
    bfs_expand_phase(&kargs, tid);
  } else {
    bfs_commit_phase(&kargs, tid);
  }
}
