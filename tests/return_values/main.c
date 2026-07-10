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
    FILE* file = fopen("return_values.wasm", "rb");
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
    static const char* entries[] = {"app_main_i32", "app_main_i64", "app_main_f32", "app_main_f64"};
    static const OsValueType types[] = {OS_VALUE_TYPE_I32, OS_VALUE_TYPE_I64, OS_VALUE_TYPE_F32, OS_VALUE_TYPE_F64};
    WasmBinary wasm = {0};
    uint32_t i;
    g_log = fopen("return_values.log", "w");
    if (!g_log)
        return 1;
    hal_init();
    expect(os_init() == OS_STATUS_OK, "initialize OS");
    expect(load_wasm(&wasm), "load return_values.wasm");
    if (wasm.bytes)
    {
        for (i = 0U; i < 4U; ++i)
        {
            OsTaskHandle task = NULL;
            OsValue value;
            OsStatus status = os_task_create(&task, wasm.bytes, wasm.size, entries[i], entries[i], 64U * 1024U,
                                             OS_TASK_PRIORITY_NORMAL);
            expect(status == OS_STATUS_OK && task != NULL, "create typed return task");
            if (!task)
                continue;
            expect(schedule_until_dead(task), "typed return task reaches DEAD");
            status = os_task_get_return_value(task, &value);
            expect(status == OS_STATUS_OK, "read return value");
            expect(value.type == types[i], "return type matches");
            if (i == 0U)
                expect(value.value.i32 == 42U, "i32 value");
            if (i == 1U)
                expect(value.value.i64 == 0x1122334455667788ULL, "i64 value");
            if (i == 2U)
                expect(value.value.f32 == 12.5f, "f32 value");
            if (i == 3U)
                expect(value.value.f64 == 123.25, "f64 value");
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
