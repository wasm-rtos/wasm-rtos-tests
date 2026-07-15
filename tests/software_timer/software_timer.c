#include <stdint.h>

#define OS_STATUS_OK 0U
#define OS_NOTIFY_INCREMENT 2U
#define OS_NOTIFY_SET_VALUE_WITH_OVERWRITE 3U
#define OS_WAIT_FOREVER UINT32_MAX

__attribute__((import_module("env"), import_name("os_task_notify_wait"))) extern uint32_t
os_task_notify_wait(
    uint32_t clear_on_entry,
    uint32_t clear_on_exit,
    uint32_t* out_value,
    uint32_t timeout_ms
);

__attribute__((import_module("env"), import_name("os_task_notify_take"))) extern uint32_t
os_task_notify_take(uint32_t clear_count_on_exit, uint32_t timeout_ms);

__attribute__((import_module("env"), import_name("os_timer_create"))) extern uint32_t
os_timer_create(
    uint32_t period_ms,
    uint32_t auto_reload,
    uint32_t target_task_id,
    uint32_t notify_value,
    uint32_t notify_action
);

__attribute__((import_module("env"), import_name("os_timer_delete"))) extern uint32_t
os_timer_delete(uint32_t timer_id);

__attribute__((import_module("env"), import_name("os_timer_start"))) extern uint32_t
os_timer_start(uint32_t timer_id);

__attribute__((import_module("env"), import_name("os_timer_stop"))) extern uint32_t
os_timer_stop(uint32_t timer_id);

__attribute__((import_module("env"), import_name("os_timer_reset"))) extern uint32_t
os_timer_reset(uint32_t timer_id);

__attribute__((import_module("env"), import_name("os_timer_change_period"))) extern uint32_t
os_timer_change_period(uint32_t timer_id, uint32_t period_ms);

__attribute__((import_module("env"), import_name("os_timer_is_active"))) extern uint32_t
os_timer_is_active(uint32_t timer_id);

uint32_t app_main_wait_forever(uint32_t unused)
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

uint32_t app_main_wait_timeout(uint32_t timeout_ms)
{
    uint32_t value = 0U;
    uint32_t status = os_task_notify_wait(
        0U,
        UINT32_MAX,
        &value,
        timeout_ms
    );

    return status == OS_STATUS_OK ? value : 0x80000000U | status;
}

uint32_t app_main_take_twice(uint32_t unused)
{
    uint32_t first;
    uint32_t second;
    (void)unused;

    first = os_task_notify_take(0U, OS_WAIT_FOREVER);
    second = os_task_notify_take(0U, OS_WAIT_FOREVER);
    return first * 10U + second;
}

uint32_t app_main_wasm_timer(uint32_t unused)
{
    uint32_t timer_id;
    uint32_t value = unused;
    uint32_t status;

    timer_id = os_timer_create(
        5U,
        0U,
        0U,
        0x5aU,
        OS_NOTIFY_SET_VALUE_WITH_OVERWRITE
    );
    if (timer_id == 0U || os_timer_is_active(timer_id) != 0U)
    {
        return 0xe001U;
    }

    if (os_timer_start(timer_id) != OS_STATUS_OK ||
        os_timer_is_active(timer_id) == 0U)
    {
        return 0xe002U;
    }

    if (os_timer_stop(timer_id) != OS_STATUS_OK ||
        os_timer_is_active(timer_id) != 0U)
    {
        return 0xe003U;
    }

    if (os_timer_reset(timer_id) != OS_STATUS_OK ||
        os_timer_change_period(timer_id, 3U) != OS_STATUS_OK)
    {
        return 0xe004U;
    }

    status = os_task_notify_wait(
        0U,
        UINT32_MAX,
        &value,
        OS_WAIT_FOREVER
    );
    if (status != OS_STATUS_OK || os_timer_is_active(timer_id) != 0U)
    {
        return 0xe005U;
    }

    if (os_timer_delete(timer_id) != OS_STATUS_OK)
    {
        return 0xe006U;
    }

    return value;
}
