#include <stdint.h>

#define ENV_IMPORT(name) __attribute__((import_module("env"), import_name(name)))
ENV_IMPORT("os_delay_ms") void os_delay_ms(uint32_t delay_ms);
ENV_IMPORT("os_get_time_ms") uint32_t os_get_time_ms(void);

/* Sleeps delay_ms and returns how long the sleep took in kernel time. */
uint32_t app_main_sleep(uint32_t delay_ms)
{
    uint32_t start = os_get_time_ms();
    os_delay_ms(delay_ms);
    return os_get_time_ms() - start;
}
