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
    FILE* file = fopen("semaphore_api.wasm", "rb");
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
    uint32_t semaphore_id
)
{
    OsValue arg;

    arg.type = OS_VALUE_TYPE_I32;
    arg.value.i32 = semaphore_id;
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
    OsSemaphoreHandle semaphore = NULL;
    OsTaskHandle waiter = NULL;
    OsTaskHandle timeout = NULL;
    OsTaskHandle immediate = NULL;
    uint32_t semaphore_id = 0U;

    g_log = fopen("semaphore_api.log", "w");
    if (g_log == NULL)
    {
        return 1;
    }

    hal_init();
    expect(os_init() == OS_STATUS_OK, "initialize OS");
    expect(load_wasm(&wasm), "load semaphore test module");
    expect(os_semaphore_create(&semaphore, 1U, 0U) == OS_STATUS_OK &&
               semaphore != NULL,
           "create binary semaphore");
    semaphore_id = os_semaphore_get_id(semaphore);

    if (wasm.bytes != NULL && semaphore != NULL)
    {
        expect(create_task(&waiter, &wasm, "app_main_wait", "waiter",
                           semaphore_id) == OS_STATUS_OK,
               "create semaphore waiter");
        expect(os_schedule() == OS_STATUS_OK, "waiter attempts take");
        expect(os_task_get_state(waiter) == OS_TASK_WAITING,
               "empty semaphore blocks task");
        expect(os_get_waiting_task_count() == 1U,
               "waiting task counter increments");

        expect(os_semaphore_give(semaphore) == OS_STATUS_OK,
               "give semaphore to waiter");
        expect(os_task_get_state(waiter) == OS_TASK_READY,
               "give wakes highest-priority waiter");
        expect(os_semaphore_get_count(semaphore) == 0U,
               "give transfers token directly");
        expect(os_schedule() == OS_STATUS_OK, "resumed waiter exits");
        expect(os_task_get_state(waiter) == OS_TASK_DEAD &&
                   os_task_get_exit_code(waiter) == OS_STATUS_OK,
               "blocked take returns success after resume");

        expect(create_task(&timeout, &wasm, "app_main_timeout", "timeout",
                           semaphore_id) == OS_STATUS_OK,
               "create timeout waiter");
        expect(os_schedule() == OS_STATUS_OK, "timeout waiter blocks");
        os_tick(4U);
        expect(os_task_get_state(timeout) == OS_TASK_WAITING,
               "waiter remains blocked before timeout");
        os_tick(1U);
        expect(os_task_get_state(timeout) == OS_TASK_READY,
               "waiter wakes exactly at timeout");
        expect(os_task_get_last_wait_status(timeout) == OS_STATUS_TIMEOUT,
               "kernel records timeout status");
        expect(os_schedule() == OS_STATUS_OK, "timeout waiter resumes");
        expect(os_task_get_exit_code(timeout) == OS_STATUS_TIMEOUT,
               "blocked take returns timeout to WASM");

        expect(os_semaphore_give(semaphore) == OS_STATUS_OK,
               "give stores token without waiter");
        expect(os_semaphore_get_count(semaphore) == 1U,
               "semaphore count increments");
        expect(os_semaphore_give(semaphore) == OS_STATUS_SEMAPHORE_FULL,
               "give detects full semaphore");

        expect(create_task(&immediate, &wasm, "app_main_immediate", "immediate",
                           semaphore_id) == OS_STATUS_OK,
               "create immediate taker");
        expect(os_schedule() == OS_STATUS_OK, "immediate taker runs");
        expect(os_task_get_state(immediate) == OS_TASK_DEAD &&
                   os_task_get_exit_code(immediate) == OS_STATUS_OK,
               "available token is taken without blocking");
        expect(os_semaphore_get_count(semaphore) == 0U,
               "take consumes stored token");

        expect(os_task_delete(waiter) == OS_STATUS_OK, "delete waiter");
        expect(os_task_delete(timeout) == OS_STATUS_OK, "delete timeout task");
        expect(os_task_delete(immediate) == OS_STATUS_OK,
               "delete immediate task");
    }

    os_semaphore_delete(semaphore);
    free(wasm.bytes);
    expect(os_get_task_count() == 0U, "OS is clean");
    os_shutdown();
    hal_shutdown();
    fclose(g_log);
    return g_failures == 0 ? 0 : 1;
}
