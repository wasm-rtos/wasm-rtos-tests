#include "os.h"
#include "hal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct WasmBinary
{
    uint8_t* bytes;
    uint32_t size;
} WasmBinary;

static FILE* g_log;
static int g_failures;

static void expect(int condition, const char* message)
{
    fprintf(stdout, "%s %s\n", condition ? "PASS" : "FAIL", message);
    fprintf(g_log, "%s %s\n", condition ? "PASS" : "FAIL", message);
    fflush(g_log);
    if (!condition)
    {
        ++g_failures;
    }
}

static int load_wasm(WasmBinary* binary)
{
    FILE* file = fopen("task_notification_api.wasm", "rb");
    long size;

    if (file == NULL || fseek(file, 0L, SEEK_END) != 0 ||
        (size = ftell(file)) <= 0L ||
        (unsigned long)size > 0xFFFFFFFFUL ||
        fseek(file, 0L, SEEK_SET) != 0)
    {
        if (file != NULL)
        {
            fclose(file);
        }
        return 0;
    }

    binary->bytes = (uint8_t*)malloc((size_t)size);
    if (binary->bytes == NULL ||
        fread(binary->bytes, 1U, (size_t)size, file) != (size_t)size)
    {
        free(binary->bytes);
        binary->bytes = NULL;
        fclose(file);
        return 0;
    }

    fclose(file);
    binary->size = (uint32_t)size;
    return 1;
}

static OsStatus create_task(
    OsTaskHandle* out_task,
    WasmBinary* wasm,
    const char* entry,
    const char* name,
    uint32_t argument,
    uint32_t priority
)
{
    OsValue arg;

    arg.type = OS_VALUE_TYPE_I32;
    arg.value.i32 = argument;
    return os_task_create_with_args(
        out_task,
        wasm->bytes,
        wasm->size,
        entry,
        &arg,
        1U,
        name,
        64U * 1024U,
        priority
    );
}

int main(void)
{
    WasmBinary wasm = {0};
    OsTaskHandle waiter = NULL;
    OsTaskHandle sender = NULL;
    OsTaskHandle taker = NULL;
    OsTaskHandle pending = NULL;
    OsTaskHandle timeout = NULL;

    g_log = fopen("task_notification_api.log", "w");
    if (g_log == NULL)
    {
        return 1;
    }

    hal_init();
    expect(os_init() == OS_STATUS_OK, "initialize OS");
    expect(load_wasm(&wasm), "load notification test module");

    if (wasm.bytes != NULL)
    {
        expect(create_task(&waiter, &wasm, "app_main_wait", "notify_waiter", 0U,
                           OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK,
               "create notification waiter");
        expect(os_schedule() == OS_STATUS_OK, "notification waiter blocks");
        expect(os_task_get_state(waiter) == OS_TASK_WAITING,
               "notification wait enters WAITING");

        expect(create_task(&sender, &wasm, "app_main_sender", "notify_sender",
                           os_task_get_id(waiter), OS_TASK_PRIORITY_HIGH) == OS_STATUS_OK,
               "create WASM notification sender");
        expect(os_schedule() == OS_STATUS_OK, "WASM sender notifies waiter");
        expect(os_task_get_state(sender) == OS_TASK_DEAD &&
                   os_task_get_exit_code(sender) == OS_STATUS_OK,
               "task notification import succeeds");
        expect(os_task_get_state(waiter) == OS_TASK_READY,
               "notification wakes waiter");
        expect(os_task_get_last_wait_value(waiter) == 0x55U,
               "kernel records notification value");

        expect(os_schedule() == OS_STATUS_OK, "notification waiter resumes");
        expect(os_task_get_state(waiter) == OS_TASK_DEAD &&
                   os_task_get_exit_code(waiter) == 0x55U,
               "wait returns status and writes value to WASM memory");
        expect(os_task_get_notification_value(waiter) == 0U &&
                   !os_task_is_notification_pending(waiter),
               "clear-on-exit consumes notification bits");

        expect(create_task(&taker, &wasm, "app_main_take", "notify_take", 0U,
                           OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK,
               "create notification take task");
        expect(os_task_notify(taker, 3U,
                              OS_NOTIFY_SET_VALUE_WITH_OVERWRITE) == OS_STATUS_OK,
               "preload notification count");
        expect(os_schedule() == OS_STATUS_OK, "notification take runs");
        expect(os_task_get_state(taker) == OS_TASK_DEAD &&
                   os_task_get_exit_code(taker) == 3U,
               "notify take returns count before decrement");
        expect(os_task_get_notification_value(taker) == 2U &&
                   !os_task_is_notification_pending(taker),
               "counting take decrements and consumes pending state");

        expect(create_task(&pending, &wasm, "app_main_pending", "notify_pending",
                           0U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK,
               "create pending notification task");
        expect(os_task_notify(pending, 0x3U, OS_NOTIFY_SET_BITS) == OS_STATUS_OK,
               "preload pending notification bits");
        expect(os_schedule() == OS_STATUS_OK,
               "task consumes already-pending notification");
        expect(os_task_get_state(pending) == OS_TASK_DEAD &&
                   os_task_get_exit_code(pending) == 0x3U,
               "clear-on-entry preserves unread pending bits");
        expect(os_task_get_notification_value(pending) == 0x3U &&
                   !os_task_is_notification_pending(pending),
               "notify wait consumes state without clear-on-exit");

        expect(create_task(&timeout, &wasm, "app_main_timeout", "notify_timeout",
                           0U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK,
               "create notification timeout task");
        expect(os_schedule() == OS_STATUS_OK, "notification timeout task blocks");
        os_tick(4U);
        expect(os_task_get_state(timeout) == OS_TASK_WAITING,
               "notification task waits before deadline");
        os_tick(1U);
        expect(os_task_get_state(timeout) == OS_TASK_READY,
               "notification task wakes at deadline");
        expect(os_task_get_last_wait_status(timeout) == OS_STATUS_TIMEOUT,
               "notification timeout status is recorded");
        expect(os_schedule() == OS_STATUS_OK, "notification timeout resumes");
        expect(os_task_get_exit_code(timeout) == OS_STATUS_TIMEOUT,
               "notify wait returns timeout to WASM");

        expect(os_task_delete(waiter) == OS_STATUS_OK, "delete waiter");
        expect(os_task_delete(sender) == OS_STATUS_OK, "delete sender");
        expect(os_task_delete(taker) == OS_STATUS_OK, "delete taker");
        expect(os_task_delete(pending) == OS_STATUS_OK,
               "delete pending notification task");
        expect(os_task_delete(timeout) == OS_STATUS_OK,
               "delete notification timeout task");
    }

    free(wasm.bytes);
    expect(os_get_task_count() == 0U, "OS is clean");
    os_shutdown();
    hal_shutdown();
    fclose(g_log);
    return g_failures == 0 ? 0 : 1;
}
