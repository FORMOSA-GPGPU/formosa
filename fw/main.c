/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* clang-format off */
#include "FreeRTOS.h" // IWYU pragma: keep
#include "portable.h"
#include "task.h"
/* clang-format on */

#include <stdio.h>
#include <stdlib.h>

#include "cp_dispatch.h"
#include "cp_hwinfo.h"
#include "cp_lmem_allocator.h"
#include "cp_memory_copy.h"
#include "cp_moving_average.h"
#include "cp_retire.h"
#include "cp_stack_remap.h"
#include "cp_status.h"
#include "cp_wgi_buffer.h"

void vAssertCalled() { /* Error */ }

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
  fprintf(stderr, "\033[31mStack overflow in task %s\033[0m\r\n", pcTaskName);

  taskDISABLE_INTERRUPTS();
  abort();
}

void vApplicationMallocFailedHook() {
  fprintf(stderr,
          "\033[31mMalloc failed: free_heap=%lu minimum_ever_free_heap=%lu"
          "\033[0m\r\n",
          (unsigned long)xPortGetFreeHeapSize(),
          (unsigned long)xPortGetMinimumEverFreeHeapSize());

  taskDISABLE_INTERRUPTS();
  abort();
}

int main() {
  cp_firmware_status_booting();

  /* Init */
  if (cp_hwinfo_init() != 0) {
    fprintf(stderr, "\033[31mHardware info initialization failed\033[0m\r\n");
    goto dead;
  }

  cp_ma_init();

  if (cp_wgi_buf_init() != 0) {
    fprintf(stderr, "\033[31mWGI buffer initialization failed\033[0m\r\n");
    goto dead;
  }

  if (cp_lmem_allocator_init() != 0) {
    fprintf(stderr,
            "\033[31mLocal memory allocator initialization failed\033[0m\r\n");
    goto dead;
  }

  for (size_t i = 0; i < g_num_sm; ++i) {
    cp_stack_remap_reset(sm_mmio_at((unsigned)i));
  }

  BaseType_t status;
  status = xTaskCreate(cp_dispatch, "cp_dispatch", 200, NULL, 6, NULL);
  if (status != pdPASS) {
    printf("Failed to create cp_dispatch task\r\n");
    goto dead;
  }

  status = xTaskCreate(cp_retire_sm, "cp_retire_sm", 200, NULL, 6, NULL);
  if (status != pdPASS) {
    printf("Failed to create cp_retire_sm task\r\n");
    goto dead;
  }

  status = xTaskCreate(cp_retire_cache, "cp_retire_cache", 200, NULL, 6, NULL);
  if (status != pdPASS) {
    printf("Failed to create cp_retire_cache task\r\n");
    goto dead;
  }

  status =
      xTaskCreate(cp_memory_copy_task, "cp_memory_copy", 200, NULL, 6, NULL);
  if (status != pdPASS) {
    printf("Failed to create cp_memory_copy task\r\n");
    goto dead;
  }

  printf("Starting scheduler...\r\n");

  vTaskStartScheduler();

dead:
  abort();
}
