#include <wasm-rtos.h>

static int value = 0;

int app_main(void)
{
    value = 7;
    wr_yield();
    value += 5;
    return value;
}
