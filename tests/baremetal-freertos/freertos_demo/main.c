#include <FreeRTOS.h>
#include <printf.h>
#include <task.h>

#define STACK_SIZE 512

extern void freertos_risc_v_trap_handler(void);

void vTask(void *pvParameters) {
  const char *taskName = (const char *)pvParameters;
  while (1) {
    printf("Hello from %s!\n", taskName);
  }
}

int main(void) {
  __asm__ volatile("csrw mtvec, %0" : : "r"(freertos_risc_v_trap_handler));

  BaseType_t xReturned;
  TaskHandle_t xHandle = NULL;
  TaskHandle_t xHandle2 = NULL;

  printf("Starting FreeRTOS demo...\n");
  // Create two tasks

  xReturned = xTaskCreate(vTask, "Task 1", STACK_SIZE, (void *)"Task 1",
                          tskIDLE_PRIORITY, &xHandle);
  if (xReturned != pdPASS) {
    printf("Task 1 creation failed!\n");
    while (1) {
    }
  }
  xReturned = xTaskCreate(vTask, "Task 2", STACK_SIZE, (void *)"Task 2",
                          tskIDLE_PRIORITY, &xHandle2);
  if (xReturned != pdPASS) {
    printf("Task 2 creation failed!\n");
    while (1) {
    }
  }
  printf("Tasks created.\n");
  // Start the scheduler
  vTaskStartScheduler();

  // Should never reach here
  printf("Scheduler ended unexpectedly!\n");
  while (1) {
  }
  return 0;
}
