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
    FILE* file = fopen("mutex_priority_inheritance.wasm", "rb");
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
    OsMutexHandle mutex = NULL;
    OsTaskHandle low = NULL;
    OsTaskHandle medium = NULL;
    OsTaskHandle high = NULL;
    uint32_t mutex_id = 0U;

    g_log = fopen("mutex_priority_inheritance.log", "w");
    if (g_log == NULL)
    {
        return 1;
    }

    hal_init();
    expect(os_init() == OS_STATUS_OK, "initialize OS");
    expect(load_wasm(&wasm), "load mutex test module");
    expect(os_mutex_create(&mutex) == OS_STATUS_OK && mutex != NULL,
           "create mutex");
    mutex_id = os_mutex_get_id(mutex);

    if (wasm.bytes != NULL && mutex != NULL)
    {
        expect(create_task(&low, &wasm, "app_main_low", "low", mutex_id,
                           OS_TASK_PRIORITY_LOW) == OS_STATUS_OK,
               "create low-priority owner");
        expect(os_schedule() == OS_STATUS_OK, "low task locks and yields");
        expect(os_mutex_get_owner(mutex) == low, "low task owns mutex");
        expect(os_task_get_state(low) == OS_TASK_READY,
               "low task is ready after yield");

        expect(create_task(&medium, &wasm, "app_main_medium", "medium", 0U,
                           OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK,
               "create medium-priority task");
        expect(create_task(&high, &wasm, "app_main_high", "high", mutex_id,
                           OS_TASK_PRIORITY_HIGH) == OS_STATUS_OK,
               "create high-priority waiter");

        expect(os_schedule() == OS_STATUS_OK, "high task blocks on mutex");
        expect(os_task_get_state(high) == OS_TASK_WAITING,
               "high task waits for mutex");
        expect(os_task_get_base_priority(low) == OS_TASK_PRIORITY_LOW,
               "owner base priority is unchanged");
        expect(os_task_get_priority(low) == OS_TASK_PRIORITY_HIGH,
               "owner inherits waiter priority");

        expect(os_schedule() == OS_STATUS_OK,
               "inherited owner runs before medium task");
        expect(os_task_get_run_count(medium) == 0U,
               "medium task does not cause priority inversion");
        expect(os_task_get_state(low) == OS_TASK_READY,
               "low task is preempted after releasing mutex");
        expect(os_mutex_get_owner(mutex) == high,
               "mutex ownership transfers to highest waiter");
        expect(os_task_get_state(high) == OS_TASK_READY,
               "high waiter becomes ready");

        expect(os_schedule() == OS_STATUS_OK, "high waiter resumes");
        expect(os_task_get_state(high) == OS_TASK_DEAD,
               "high waiter exits after unlock");
        expect(os_task_get_exit_code(high) == 0U,
               "blocked mutex call returns success after resume");
        expect(os_mutex_get_owner(mutex) == NULL, "mutex is released");

        expect(os_schedule() == OS_STATUS_OK, "medium task finally runs");
        expect(os_task_get_state(medium) == OS_TASK_DEAD,
               "medium task exits normally");
        expect(os_schedule() == OS_STATUS_OK,
               "preempted low-priority task finishes");
        expect(os_task_get_state(low) == OS_TASK_DEAD,
               "low task exits after higher priorities complete");
        expect(os_task_get_exit_code(low) == 0U &&
                   os_task_get_exit_code(medium) == 0U,
               "all mutex test tasks return success");

        expect(os_task_delete(low) == OS_STATUS_OK, "delete low task");
        expect(os_task_delete(medium) == OS_STATUS_OK, "delete medium task");
        expect(os_task_delete(high) == OS_STATUS_OK, "delete high task");
    }

    os_mutex_delete(mutex);
    free(wasm.bytes);
    expect(os_get_task_count() == 0U, "OS is clean");
    os_shutdown();
    hal_shutdown();
    fclose(g_log);
    return g_failures == 0 ? 0 : 1;
}
