#include "console.h"

int main(void) {
  demo_console_init();
  tfp_printf("Hello, bare-metal command-processor!\n");
  return 0;
}
