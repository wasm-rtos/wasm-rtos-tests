#include <stdint.h>

#define ENV_IMPORT(name) __attribute__((import_module("env"), import_name(name)))
ENV_IMPORT("host_yield") void host_yield(void);
ENV_IMPORT("host_preempt") void host_preempt(void);
ENV_IMPORT("host_publish") void host_publish(uint32_t address);

static volatile uint32_t counter;

/* Calls a host import that yields, rounds times. */
uint32_t app_main_yield(uint32_t rounds)
{
    uint32_t i;
    for (i = 0U; i < rounds; ++i)
    {
        ++counter;
        host_yield();
    }
    return counter;
}

/* Calls a host import that requests preemption, rounds times. */
uint32_t app_main_preempt(uint32_t rounds)
{
    uint32_t i;
    for (i = 0U; i < rounds; ++i)
    {
        ++counter;
        host_preempt();
    }
    return counter;
}

/* Publishes its counter and counts forever. */
void app_main_count(void)
{
    host_publish((uint32_t)(uintptr_t)&counter);
    for (;;)
        ++counter;
}
