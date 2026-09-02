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
#include "cp_panic.h"
#include "cp_retire.h"
#include "cp_stack_remap.h"
#include "cp_status.h"
#include "cp_wgi_buffer.h"

void vAssertCalled(const char *file, int line) {
  cp_panic("FreeRTOS configASSERT failed at %s:%d", file, line);
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
  cp_panic("Stack overflow in task %s", pcTaskName);
}

void vApplicationMallocFailedHook() {
  cp_panic("Malloc failed: free_heap=%lu minimum_ever_free_heap=%lu",
           (unsigned long)xPortGetFreeHeapSize(),
           (unsigned long)xPortGetMinimumEverFreeHeapSize());
}

int main() {
  cp_firmware_status_booting();

  /* Init */
  cp_hwinfo_init();

  cp_ma_init();

  if (cp_wgi_buf_init() != 0) {
    cp_panic("WGI buffer initialization failed");
  }

  if (cp_lmem_allocator_init() != 0) {
    cp_panic("Local memory allocator initialization failed");
  }

  for (size_t i = 0; i < g_num_sm; ++i) {
    cp_stack_remap_reset(sm_mmio_at((unsigned)i));
  }

  BaseType_t status;
  status = xTaskCreate(cp_dispatch, "cp_dispatch", 200, NULL, 6, NULL);
  if (status != pdPASS) {
    cp_panic("Failed to create cp_dispatch task");
  }

  status = xTaskCreate(cp_retire_sm, "cp_retire_sm", 200, NULL, 6, NULL);
  if (status != pdPASS) {
    cp_panic("Failed to create cp_retire_sm task");
  }

  status = xTaskCreate(cp_retire_cache, "cp_retire_cache", 200, NULL, 6, NULL);
  if (status != pdPASS) {
    cp_panic("Failed to create cp_retire_cache task");
  }

  status =
      xTaskCreate(cp_memory_copy_task, "cp_memory_copy", 200, NULL, 6, NULL);
  if (status != pdPASS) {
    cp_panic("Failed to create cp_memory_copy task");
  }

  printf("Starting scheduler...\r\n");

  vTaskStartScheduler();

  /* Should never reach here */
  cp_panic("Scheduler exited unexpectedly");
}
