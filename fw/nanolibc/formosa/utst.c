/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int utst() {
  char hello[] = "hello";
  printf("%s\n", hello);
  char *tmp = calloc(1, 33);
  char *m = malloc(517);
  char *m1 = malloc(5000);
  if (!m1) {
    printf("m1 is NULL\n");
  }
  printf("%p\n", tmp);
  memset(tmp, 'F', 32);
  printf("%s %ld\n", tmp, strlen(tmp));
  strncpy(tmp, hello, 6);
  printf("%s %ld\n", tmp, strlen(tmp));
  int i = 0;
  int j = strlen(tmp) - 1;
  while (i < j) {
    tmp[i] ^= tmp[j];
    tmp[j] ^= tmp[i];
    tmp[i] ^= tmp[j];
    i++;
    j--;
  }
  printf("%s %ld\n", tmp, strlen(tmp));

  printf("Hello from firmware %d %x\n", 123, 123);
  free(m1);
  free(m);
  free(tmp);
  return 0;
}
