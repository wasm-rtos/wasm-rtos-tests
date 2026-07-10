#include "os.h"
#include "hal.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static int load_wasm(const char* path, WasmBinary* binary)
{
    FILE* file = fopen(path, "rb");
    long size;

    binary->bytes = NULL;
    binary->size = 0U;

    if (file == NULL)
    {
        expect(0, "open bad_import.wasm");
        return 0;
    }

    if (fseek(file, 0L, SEEK_END) != 0 || (size = ftell(file)) <= 0L ||
        (unsigned long)size > 0xFFFFFFFFUL || fseek(file, 0L, SEEK_SET) != 0)
    {
        fclose(file);
        expect(0, "read bad_import.wasm size");
        return 0;
    }

    binary->bytes = (uint8_t*)malloc((size_t)size);
    if (binary->bytes == NULL || fread(binary->bytes, 1U, (size_t)size, file) != (size_t)size)
    {
        fclose(file);
        free(binary->bytes);
        binary->bytes = NULL;
        expect(0, "read bad_import.wasm contents");
        return 0;
    }

    fclose(file);
    binary->size = (uint32_t)size;
    return 1;
}

int main(void)
{
    WasmBinary wasm;
    OsTaskHandle task = NULL;
    OsStatus status;
    const char* phase;
    const char* result;

    g_log = fopen("bad_import.log", "w");
    if (g_log == NULL)
    {
        return 1;
    }

    hal_init();
    expect(os_init() == OS_STATUS_OK, "initialize OS");

    if (load_wasm("bad_import.wasm", &wasm))
    {
        status = os_task_create(
            &task,
            wasm.bytes,
            wasm.size,
            "app_main",
            "bad_import",
            64U * 1024U,
            OS_TASK_PRIORITY_NORMAL
        );

        phase = os_get_last_error_phase();
        result = os_get_last_error_result();

        expect(status == OS_STATUS_WASM_ERROR, "task creation reports WASM error");
        expect(task == NULL, "failed creation returns null task");
        expect(os_get_last_error_status() == OS_STATUS_WASM_ERROR, "diagnostic status is WASM error");
        expect(phase != NULL && strcmp(phase, "none") != 0, "diagnostic phase is recorded");
        expect(result != NULL && result[0] != '\0', "diagnostic result is recorded");
        expect(os_get_task_count() == 0U, "failed task was not inserted");

        free(wasm.bytes);
    }

    expect(
        os_get_task_count() == 0U &&
        os_get_ready_task_count() == 0U &&
        os_get_waiting_task_count() == 0U &&
        os_task_get_current() == NULL,
        "OS remains empty"
    );

    log_message("%s bad_import", g_failures == 0 ? "PASS" : "FAIL");
    os_shutdown();
    hal_shutdown();
    fclose(g_log);

    return g_failures == 0 ? 0 : 1;
}
