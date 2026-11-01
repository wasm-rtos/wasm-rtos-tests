#include <wasm-rtos.h>

/*
 * Task notifications are a host API; the test registers these imports to
 * hand them to the guest.
 */
#define TEST_IMPORT(name) __attribute__((import_module("env"), import_name(name)))
TEST_IMPORT("notify_wait") uint32_t notify_wait(uint32_t clear_on_entry, uint32_t clear_on_exit, uint32_t timeout_ms);
TEST_IMPORT("notify_value") uint32_t notify_value(void);
TEST_IMPORT("notify_take") uint32_t notify_take(uint32_t clear_count_on_exit, uint32_t timeout_ms);

#define OS_STATUS_OK 0U

uint32_t app_main_wait_forever(uint32_t unused)
{
    uint32_t status = notify_wait(0U, UINT32_MAX, WR_WAIT_FOREVER);
    (void)unused;
    return status == OS_STATUS_OK ? notify_value() : 0x80000000U | status;
}

uint32_t app_main_wait_timeout(uint32_t timeout_ms)
{
    uint32_t status = notify_wait(0U, UINT32_MAX, timeout_ms);
    return status == OS_STATUS_OK ? notify_value() : 0x80000000U | status;
}

uint32_t app_main_take_twice(uint32_t unused)
{
    uint32_t first;
    uint32_t second;
    (void)unused;

    first = notify_take(0U, WR_WAIT_FOREVER);
    second = notify_take(0U, WR_WAIT_FOREVER);
    return first * 10U + second;
}

/* A program's own timer through the ABI: counts expirations, no notifications. */
uint32_t app_main_wasm_timer(uint32_t unused)
{
    int32_t timer = wr_timer_create(5U, 0U);
    (void)unused;

    if (timer <= 0)
        return 0xe001U;
    if (wr_timer_start(timer) != 0 || wr_timer_stop(timer) != 0)
        return 0xe002U;
    if (wr_timer_set_period(timer, 3U) != 0 || wr_timer_start(timer) != 0)
        return 0xe003U;
    if (wr_timer_read(timer) != 0)
        return 0xe004U;
    if (wr_wait_any(&timer, 1U, WR_WAIT_FOREVER) != 0)
        return 0xe005U;
    if (wr_timer_read(timer) != 1 || wr_timer_read(timer) != 0)
        return 0xe006U;
    if (wr_close(timer) != 0)
        return 0xe007U;
    return 0x5aU;
}
