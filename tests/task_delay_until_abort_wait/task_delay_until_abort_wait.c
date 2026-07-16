#include <stdint.h>

#define OS_STATUS_OK 0U
#define OS_STATUS_INVALID_ARGUMENT 2U
#define OS_STATUS_ABORTED 22U
#define OS_WAIT_FOREVER UINT32_MAX

__attribute__((import_module("env"), import_name("os_delay_ms"))) extern void
os_delay_ms(uint32_t delay_ms);

__attribute__((import_module("env"), import_name("os_get_time_ms"))) extern uint32_t
os_get_time_ms(void);

__attribute__((import_module("env"), import_name("os_task_delay_until"))) extern uint32_t
os_task_delay_until(uint32_t* previous_wake_time_ms, uint32_t period_ms);

__attribute__((import_module("env"), import_name("os_task_abort_wait"))) extern uint32_t
os_task_abort_wait(uint32_t task_id);

__attribute__((import_module("env"), import_name("os_semaphore_create"))) extern uint32_t
os_semaphore_create(uint32_t max_count, uint32_t initial_count);

__attribute__((import_module("env"), import_name("os_semaphore_delete"))) extern uint32_t
os_semaphore_delete(uint32_t semaphore_id);

__attribute__((import_module("env"), import_name("os_semaphore_take"))) extern uint32_t
os_semaphore_take(uint32_t semaphore_id, uint32_t timeout_ms);

__attribute__((import_module("env"), import_name("os_mutex_lock"))) extern uint32_t
os_mutex_lock(uint32_t mutex_id, uint32_t timeout_ms);

__attribute__((import_module("env"), import_name("os_mutex_unlock"))) extern uint32_t
os_mutex_unlock(uint32_t mutex_id);

uint32_t app_main_periodic(uint32_t unused)
{
    uint32_t phase_ms = os_get_time_ms();
    uint32_t index;
    (void)unused;

    for (index = 0U; index < 4U; ++index)
    {
        uint32_t status = os_task_delay_until(&phase_ms, 10U);
        if (status != OS_STATUS_OK)
        {
            return 0xe000U | status;
        }
    }

    return os_get_time_ms();
}

uint32_t app_main_invalid_period(uint32_t unused)
{
    uint32_t phase_ms = os_get_time_ms();
    (void)unused;
    return os_task_delay_until(&phase_ms, 0U);
}

uint32_t app_main_abort_periodic(uint32_t unused)
{
    uint32_t phase_ms = os_get_time_ms();
    (void)unused;
    return os_task_delay_until(&phase_ms, 100U);
}

uint32_t app_main_abort_delay(uint32_t unused)
{
    (void)unused;
    os_delay_ms(100U);
    return 0x11U;
}

uint32_t app_main_abort_semaphore(uint32_t unused)
{
    uint32_t semaphore_id;
    uint32_t status;
    (void)unused;

    semaphore_id = os_semaphore_create(1U, 0U);
    if (semaphore_id == 0U)
    {
        return 0xe100U;
    }

    status = os_semaphore_take(semaphore_id, OS_WAIT_FOREVER);
    if (os_semaphore_delete(semaphore_id) != OS_STATUS_OK)
    {
        return 0xe101U;
    }
    return status;
}

uint32_t app_main_abort_controller(uint32_t task_id)
{
    return os_task_abort_wait(task_id);
}

uint32_t app_main_mutex_owner(uint32_t mutex_id)
{
    uint32_t status = os_mutex_lock(mutex_id, 0U);
    if (status != OS_STATUS_OK)
    {
        return 0xe200U | status;
    }

    os_delay_ms(100U);
    status = os_mutex_unlock(mutex_id);
    return status;
}

uint32_t app_main_mutex_waiter(uint32_t mutex_id)
{
    return os_mutex_lock(mutex_id, OS_WAIT_FOREVER);
}
