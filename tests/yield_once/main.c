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
        ++g_failures;
}

static int load_wasm(WasmBinary* binary)
{
    FILE* file = fopen("yield_once.wasm", "rb");
    long size;
    if (!file)
        return 0;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    rewind(file);
    if (size <= 0)
    {
        fclose(file);
        return 0;
    }
    binary->bytes = (uint8_t*)malloc((size_t)size);
    if (!binary->bytes)
    {
        fclose(file);
        return 0;
    }
    if (fread(binary->bytes, 1U, (size_t)size, file) != (size_t)size)
    {
        free(binary->bytes);
        fclose(file);
        return 0;
    }
    fclose(file);
    binary->size = (uint32_t)size;
    return 1;
}

static int schedule_until_dead(OsTaskHandle task)
{
    uint32_t i;
    for (i = 0U; i < 10U; ++i)
    {
        if (os_schedule() != OS_STATUS_OK)
            return 0;
        if (os_task_get_state(task) == OS_TASK_DEAD)
            return 1;
    }
    return 0;
}

int main(void)
{
    WasmBinary wasm = {0};
    OsTaskHandle task = NULL;
    OsStatus status;
    g_log = fopen("yield_once.log", "w");
    if (!g_log)
        return 1;
    hal_init();
    expect(os_init() == OS_STATUS_OK, "initialize OS");
    expect(load_wasm(&wasm), "load yield_once.wasm");
    if (wasm.bytes)
    {
        status = os_task_create(&task, wasm.bytes, wasm.size, "app_main", "yield_once", 64U * 1024U,
                                OS_TASK_PRIORITY_NORMAL);
        expect(status == OS_STATUS_OK && task != NULL, "create task");
        if (task)
        {
            expect(os_schedule() == OS_STATUS_OK, "run until yield");
            expect(os_task_get_state(task) == OS_TASK_READY, "yield returns task to READY");
            expect(os_task_get_run_count(task) == 1U, "one scheduler slice consumed");
            expect(schedule_until_dead(task), "resume task after yield");
            expect(os_task_get_exit_reason(task) == OS_TASK_EXIT_RETURNED, "task returned normally");
            expect(os_task_get_exit_code(task) == 0U, "task returned zero");
            expect(os_task_delete(task) == OS_STATUS_OK, "delete task");
        }
        free(wasm.bytes);
    }
    expect(os_get_task_count() == 0U, "OS is clean");
    os_shutdown();
    hal_shutdown();
    fclose(g_log);
    return g_failures == 0 ? 0 : 1;
}
