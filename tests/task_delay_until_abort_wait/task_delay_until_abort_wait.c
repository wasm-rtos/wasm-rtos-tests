#include <wasm-rtos.h>

/*
 * Periodic delays and aborting another task's wait are host APIs; the test
 * registers these imports to hand them to the guest.
 */
#define TEST_IMPORT(name) __attribute__((import_module("env"), import_name(name)))
TEST_IMPORT("delay_until") uint32_t delay_until(uint32_t* previous_wake_time_ms, uint32_t period_ms);
TEST_IMPORT("abort_wait") uint32_t abort_wait(uint32_t task_id);

uint32_t app_main_periodic(uint32_t unused)
{
    uint32_t phase_ms = (uint32_t)wr_time_ms();
    uint32_t index;
    (void)unused;

    for (index = 0U; index < 4U; ++index)
    {
        uint32_t status = delay_until(&phase_ms, 10U);
        if (status != 0U)
        {
            return 0xe000U | status;
        }
    }

    return (uint32_t)wr_time_ms();
}

uint32_t app_main_invalid_period(uint32_t unused)
{
    uint32_t phase_ms = (uint32_t)wr_time_ms();
    (void)unused;
    return delay_until(&phase_ms, 0U);
}

uint32_t app_main_abort_periodic(uint32_t unused)
{
    uint32_t phase_ms = (uint32_t)wr_time_ms();
    (void)unused;
    return delay_until(&phase_ms, 100U);
}

uint32_t app_main_abort_delay(uint32_t unused)
{
    (void)unused;
    wr_sleep_ms(100U);
    return 0x11U;
}

int32_t app_main_abort_semaphore(uint32_t unused)
{
    int32_t semaphore = wr_semaphore_create(0, 0U, 1U, 0U);
    int32_t status;
    (void)unused;

    if (semaphore <= 0)
        return 0xe100;
    status = wr_semaphore_take(semaphore, WR_WAIT_FOREVER);
    if (wr_close(semaphore) != 0)
        return 0xe101;
    return status;
}

uint32_t app_main_abort_controller(uint32_t task_id)
{
    return abort_wait(task_id);
}

/* owner and waiter share the host's mutex "m". */
int32_t app_main_mutex_owner(uint32_t unused)
{
    int32_t mutex = wr_mutex_open("m");
    int32_t status = mutex < 0 ? mutex : wr_mutex_lock(mutex, 0U);
    (void)unused;

    if (status != 0)
        return 0xe200 | -status;
    wr_sleep_ms(100U);
    return wr_mutex_unlock(mutex);
}

int32_t app_main_mutex_waiter(uint32_t unused)
{
    int32_t mutex = wr_mutex_open("m");
    (void)unused;
    return mutex < 0 ? mutex : wr_mutex_lock(mutex, WR_WAIT_FOREVER);
}
