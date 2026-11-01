#include <stdint.h>

/*
 * Task notifications are a host API; the test registers these imports to
 * hand them to the guest.
 */
#define OS_NOTIFY_SET_BITS 1U
#define OS_STATUS_OK 0U
#define OS_WAIT_FOREVER UINT32_MAX

#define TEST_IMPORT(name) __attribute__((import_module("env"), import_name(name)))
TEST_IMPORT("notify") uint32_t notify(uint32_t task_id, uint32_t value, uint32_t action);
TEST_IMPORT("notify_wait") uint32_t notify_wait(uint32_t clear_on_entry, uint32_t clear_on_exit, uint32_t timeout_ms);
TEST_IMPORT("notify_value") uint32_t notify_value(void);
TEST_IMPORT("notify_take") uint32_t notify_take(uint32_t clear_count_on_exit, uint32_t timeout_ms);

uint32_t app_main_wait(uint32_t unused)
{
    uint32_t status = notify_wait(0U, UINT32_MAX, OS_WAIT_FOREVER);
    (void)unused;
    return status == OS_STATUS_OK ? notify_value() : 0x80000000U | status;
}

uint32_t app_main_sender(uint32_t target_task_id)
{
    return notify(target_task_id, 0x55U, OS_NOTIFY_SET_BITS);
}

uint32_t app_main_take(uint32_t unused)
{
    (void)unused;
    return notify_take(0U, 0U);
}

uint32_t app_main_pending(uint32_t unused)
{
    uint32_t status = notify_wait(0x1U, 0U, 0U);
    (void)unused;
    return status == OS_STATUS_OK ? notify_value() : 0x80000000U | status;
}

uint32_t app_main_timeout(uint32_t unused)
{
    (void)unused;
    return notify_wait(0U, 0U, 5U);
}
