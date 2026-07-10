#include "os.h"
#include "hal.h"
#include "wasm3/source/m3_env.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct WasmBinary
{
    uint8_t* bytes;
    uint32_t size;
} WasmBinary;

static FILE* g_log = NULL;
static int g_failures = 0;

static void log_message(const char* format, ...)
{
    va_list args;

    va_start(args, format);
    vprintf(format, args);
    printf("\n");
    va_end(args);

    if (g_log != NULL)
    {
        va_start(args, format);
        vfprintf(g_log, format, args);
        fprintf(g_log, "\n");
        fflush(g_log);
        va_end(args);
    }
}

static void expect(int condition, const char* message)
{
    log_message("%s %s", condition ? "PASS" : "FAIL", message);
    if (!condition)
    {
        ++g_failures;
    }
}

static int load_wasm(WasmBinary* binary)
{
    FILE* file = fopen("custom_import.wasm", "rb");
    long size;

    binary->bytes = NULL;
    binary->size = 0U;

    if (file == NULL || fseek(file, 0L, SEEK_END) != 0 || (size = ftell(file)) <= 0L ||
        (unsigned long)size > 0xFFFFFFFFUL || fseek(file, 0L, SEEK_SET) != 0)
    {
        if (file != NULL)
            fclose(file);
        expect(0, "open custom_import.wasm");
        return 0;
    }

    binary->bytes = (uint8_t*)malloc((size_t)size);
    if (binary->bytes == NULL || fread(binary->bytes, 1U, (size_t)size, file) != (size_t)size)
    {
        fclose(file);
        free(binary->bytes);
        binary->bytes = NULL;
        expect(0, "read custom_import.wasm");
        return 0;
    }

    fclose(file);
    binary->size = (uint32_t)size;
    return 1;
}

static int schedule_until_dead(OsTaskHandle task)
{
    uint32_t iteration;

    for (iteration = 0U; iteration < 10U; ++iteration)
    {
        if (os_schedule() != OS_STATUS_OK)
        {
            return 0;
        }
        if (os_task_get_state(task) == OS_TASK_DEAD)
        {
            return 1;
        }
    }

    return 0;
}

static m3ApiRawFunction(host_test_value)
{
    m3ApiReturnType(uint32_t);
    m3ApiGetArg(uint32_t, input);
    (void)runtime;
    (void)_ctx;
    (void)_mem;
    m3ApiReturn(input + 42U);
}

int main(void)
{
    WasmBinary wasm;
    OsTaskHandle task = NULL;
    OsStatus status;

    g_log = fopen("custom_import.log", "w");
    if (g_log == NULL)
    {
        return 1;
    }

    hal_init();
    expect(os_init() == OS_STATUS_OK, "initialize OS");

    status = os_host_import_register("env", "host_test_value", "i(i)", host_test_value);
    expect(status == OS_STATUS_OK, "register env.host_test_value");

    if (load_wasm(&wasm))
    {
        status = os_task_create(&task, wasm.bytes, wasm.size, "app_main", "custom_import", 64U * 1024U,
                                OS_TASK_PRIORITY_NORMAL);
        expect(status == OS_STATUS_OK && task != NULL, "create task with custom import");

        if (task != NULL)
        {
            expect(schedule_until_dead(task), "task reaches DEAD");
            expect(os_task_get_exit_reason(task) == OS_TASK_EXIT_RETURNED, "task returned normally");
            expect(os_task_get_exit_code(task) == 0U, "guest observed 37 + 42 = 79");
            expect(os_task_delete(task) == OS_STATUS_OK, "delete task");
        }

        free(wasm.bytes);
    }

    os_host_import_clear_all();
    expect(os_get_task_count() == 0U, "OS has no remaining tasks");

    log_message("%s custom_import", g_failures == 0 ? "PASS" : "FAIL");
    os_shutdown();
    hal_shutdown();
    fclose(g_log);

    return g_failures == 0 ? 0 : 1;
}
