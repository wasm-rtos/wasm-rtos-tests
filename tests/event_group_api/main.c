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
    FILE* file = fopen("event_group_api.wasm", "rb");
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
    uint32_t event_group_id
)
{
    OsValue arg;

    arg.type = OS_VALUE_TYPE_I32;
    arg.value.i32 = event_group_id;
    return os_task_create_with_args(
        out_task,
        wasm->bytes,
        wasm->size,
        entry,
        &arg,
        1U,
        name,
        64U * 1024U,
        OS_TASK_PRIORITY_NORMAL
    );
}

int main(void)
{
    WasmBinary wasm = {0};
    OsEventGroupHandle event_group = NULL;
    OsTaskHandle waiter = NULL;
    OsTaskHandle timeout = NULL;
    uint32_t event_group_id = 0U;

    g_log = fopen("event_group_api.log", "w");
    if (g_log == NULL)
    {
        return 1;
    }

    hal_init();
    expect(os_init() == OS_STATUS_OK, "initialize OS");
    expect(load_wasm(&wasm), "load event group test module");
    expect(os_event_group_create(&event_group) == OS_STATUS_OK &&
               event_group != NULL,
           "create event group");
    event_group_id = os_event_group_get_id(event_group);

    if (wasm.bytes != NULL && event_group != NULL)
    {
        expect(create_task(&waiter, &wasm, "app_main_wait_all", "wait_all",
                           event_group_id) == OS_STATUS_OK,
               "create wait-all task");
        expect(os_schedule() == OS_STATUS_OK, "event waiter blocks");
        expect(os_task_get_state(waiter) == OS_TASK_WAITING,
               "wait-all task enters WAITING");

        expect(os_event_group_set_bits(event_group, 0x1U) == 0x1U,
               "set first event bit");
        expect(os_task_get_state(waiter) == OS_TASK_WAITING,
               "wait-all task stays blocked with one bit");
        expect(os_event_group_set_bits(event_group, 0x2U) == 0U,
               "set second bit and clear matched bits on exit");
        expect(os_task_get_state(waiter) == OS_TASK_READY,
               "all requested bits wake task");
        expect(os_event_group_get_bits(event_group) == 0U,
               "clear-on-exit clears requested bits");
        expect(os_task_get_last_wait_value(waiter) == 0x3U,
               "waiter receives pre-clear bit snapshot");

        expect(os_schedule() == OS_STATUS_OK, "event waiter resumes");
        expect(os_task_get_state(waiter) == OS_TASK_DEAD &&
                   os_task_get_exit_code(waiter) == 0x3U,
               "WASM wait returns matched event bits");

        expect(create_task(&timeout, &wasm, "app_main_timeout", "event_timeout",
                           event_group_id) == OS_STATUS_OK,
               "create event timeout task");
        expect(os_schedule() == OS_STATUS_OK, "event timeout task blocks");
        os_tick(4U);
        expect(os_task_get_state(timeout) == OS_TASK_WAITING,
               "event task remains blocked before deadline");
        os_tick(1U);
        expect(os_task_get_state(timeout) == OS_TASK_READY,
               "event task wakes at deadline");
        expect(os_task_get_last_wait_status(timeout) == OS_STATUS_TIMEOUT,
               "event timeout status is recorded");
        expect(os_schedule() == OS_STATUS_OK, "event timeout task resumes");
        expect(os_task_get_exit_code(timeout) == 0U,
               "timed-out wait returns current event bits");

        expect(os_event_group_set_bits(event_group, 0x8U) == 0x8U,
               "set unrelated bit");
        expect(os_event_group_clear_bits(event_group, 0x8U) == 0x8U,
               "clear returns previous event bits");
        expect(os_event_group_get_bits(event_group) == 0U,
               "event bit is cleared");

        expect(os_task_delete(waiter) == OS_STATUS_OK, "delete event waiter");
        expect(os_task_delete(timeout) == OS_STATUS_OK,
               "delete event timeout task");
    }

    os_event_group_delete(event_group);
    free(wasm.bytes);
    expect(os_get_task_count() == 0U, "OS is clean");
    os_shutdown();
    hal_shutdown();
    fclose(g_log);
    return g_failures == 0 ? 0 : 1;
}
