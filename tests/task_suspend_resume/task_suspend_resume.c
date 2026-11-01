#include <stdint.h>

#define ENV_IMPORT(name) __attribute__((import_module("env"), import_name(name)))
ENV_IMPORT("os_delay_ms") void os_delay_ms(uint32_t delay_ms);
ENV_IMPORT("os_get_time_ms") uint32_t os_get_time_ms(void);
ENV_IMPORT("os_queue_receive_wait") uint32_t os_queue_receive_wait(uint32_t queue_id, uint32_t* item, uint32_t timeout_ms);

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
    uint32_t start = os_get_time_ms();
    os_delay_ms(1000U);
    return os_get_time_ms() - start;
}

/* Waits forever for a queue item and returns the wait status. */
uint32_t app_main_receive(uint32_t queue_id)
{
    uint32_t item = 0U;
    return os_queue_receive_wait(queue_id, &item, 0xFFFFFFFFU);
}
