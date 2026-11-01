#include <wasm-rtos.h>

/* Never returns; the host counts its scheduler slices. */
void app_main_spin(void)
{
    volatile uint32_t counter = 0U;
    for (;;)
        ++counter;
}

/* Sleeps 1000 ms and returns how long the sleep really took. */
uint32_t app_main_sleep(void)
{
    uint32_t start = (uint32_t)wr_time_ms();
    wr_sleep_ms(1000U);
    return (uint32_t)wr_time_ms() - start;
}

/* Waits forever for an item of the host's "inbox" queue and returns the result. */
int32_t app_main_receive(void)
{
    uint32_t item = 0U;
    int32_t queue = wr_queue_open("inbox", sizeof(item), 1U);
    if (queue < 0)
        return queue;
    return wr_queue_receive(queue, &item, WR_WAIT_FOREVER);
}
