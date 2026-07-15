#include <stdint.h>

#define OS_NOTIFY_SET_BITS 1U
#define OS_STATUS_OK 0U
#define OS_WAIT_FOREVER UINT32_MAX

__attribute__((import_module("env"), import_name("os_task_notify"))) extern uint32_t
os_task_notify(uint32_t task_id, uint32_t value, uint32_t action);
__attribute__((import_module("env"), import_name("os_task_notify_wait"))) extern uint32_t
os_task_notify_wait(
    uint32_t clear_on_entry,
    uint32_t clear_on_exit,
    uint32_t* out_value,
    uint32_t timeout_ms
);
__attribute__((import_module("env"), import_name("os_task_notify_take"))) extern uint32_t
os_task_notify_take(uint32_t clear_count_on_exit, uint32_t timeout_ms);

uint32_t app_main_wait(uint32_t unused)
{
    uint32_t value = unused;
    uint32_t status = os_task_notify_wait(
        0U,
        UINT32_MAX,
        &value,
        OS_WAIT_FOREVER
    );

    return status == OS_STATUS_OK ? value : 0x80000000U | status;
}

uint32_t app_main_sender(uint32_t target_task_id)
{
    return os_task_notify(
        target_task_id,
        0x55U,
        OS_NOTIFY_SET_BITS
    );
}

uint32_t app_main_take(uint32_t unused)
{
    (void)unused;
    return os_task_notify_take(0U, 0U);
}

uint32_t app_main_pending(uint32_t unused)
{
    uint32_t value = unused;
    uint32_t status = os_task_notify_wait(0x1U, 0U, &value, 0U);

    return status == OS_STATUS_OK ? value : 0x80000000U | status;
}

uint32_t app_main_timeout(uint32_t unused)
{
    uint32_t value = unused;
    return os_task_notify_wait(0U, 0U, &value, 5U);
}
