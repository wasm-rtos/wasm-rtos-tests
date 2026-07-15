#include <stdint.h>

#define OS_STATUS_OK 0U
#define OS_WAIT_FOREVER UINT32_MAX

__attribute__((import_module("env"), import_name("os_yield"))) extern void os_yield(void);
__attribute__((import_module("env"), import_name("os_mutex_lock"))) extern uint32_t
os_mutex_lock(uint32_t mutex_id, uint32_t timeout_ms);
__attribute__((import_module("env"), import_name("os_mutex_unlock"))) extern uint32_t
os_mutex_unlock(uint32_t mutex_id);

uint32_t app_main_low(uint32_t mutex_id)
{
    uint32_t status = os_mutex_lock(mutex_id, OS_WAIT_FOREVER);

    if (status != OS_STATUS_OK)
    {
        return 1U;
    }

    os_yield();
    status = os_mutex_unlock(mutex_id);
    return status == OS_STATUS_OK ? 0U : 2U;
}

uint32_t app_main_medium(uint32_t unused)
{
    volatile uint32_t value = unused;
    uint32_t index = 0U;

    for (index = 0U; index < 100U; ++index)
    {
        value += index;
    }

    return value == UINT32_MAX ? 1U : 0U;
}

uint32_t app_main_high(uint32_t mutex_id)
{
    uint32_t status = os_mutex_lock(mutex_id, OS_WAIT_FOREVER);

    if (status != OS_STATUS_OK)
    {
        return 3U;
    }

    status = os_mutex_unlock(mutex_id);
    return status == OS_STATUS_OK ? 0U : 4U;
}
