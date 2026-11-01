#include <wasm-rtos.h>

/* Sleeps delay_ms and returns how long the sleep took in kernel time. */
uint32_t app_main_sleep(uint32_t delay_ms)
{
    uint32_t start = (uint32_t)wr_time_ms();
    wr_sleep_ms(delay_ms);
    return (uint32_t)wr_time_ms() - start;
}
