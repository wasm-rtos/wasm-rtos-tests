#include <wasm-rtos.h>

void app_main(void)
{
    volatile unsigned int before = 1U;
    wr_sleep_ms(10U);
    volatile unsigned int after = before + 1U;
    (void)after;
}
