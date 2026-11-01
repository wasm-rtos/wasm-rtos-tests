#include <wasm-rtos.h>

/* low and high share the host's mutex "m". */
uint32_t app_main_low(uint32_t unused)
{
    int32_t mutex = wr_mutex_open("m");
    (void)unused;

    if (mutex < 0 || wr_mutex_lock(mutex, WR_WAIT_FOREVER) != 0)
        return 1U;
    wr_yield();
    return wr_mutex_unlock(mutex) == 0 ? 0U : 2U;
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

uint32_t app_main_high(uint32_t unused)
{
    int32_t mutex = wr_mutex_open("m");
    (void)unused;

    if (mutex < 0 || wr_mutex_lock(mutex, WR_WAIT_FOREVER) != 0)
        return 3U;
    /* Only the owner may unlock. */
    if (wr_mutex_unlock(mutex) != 0 || wr_mutex_unlock(mutex) != -WR_ENOTOWNER)
        return 4U;
    return 0U;
}
