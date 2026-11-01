#include <stdint.h>

#define ENV_IMPORT(name) __attribute__((import_module("env"), import_name(name)))
ENV_IMPORT("os_delay_ms") void os_delay_ms(uint32_t delay_ms);
ENV_IMPORT("host_publish") void host_publish(uint32_t address);

static uint8_t buffer[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };

/*
 * Tells the host where buffer lives, sleeps while the host reads and rewrites
 * it, then returns the sum of what it finds there.
 */
uint32_t app_main(void)
{
    uint32_t sum = 0U;
    uint32_t i;

    host_publish((uint32_t)(uintptr_t)buffer);
    os_delay_ms(10U);
    for (i = 0U; i < sizeof(buffer); ++i)
        sum += ((volatile uint8_t*)buffer)[i];
    return sum;
}
