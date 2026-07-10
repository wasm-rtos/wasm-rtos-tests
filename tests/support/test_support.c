#include "test_support.h"

#include "hal.h"

#include <stdarg.h>
#include <stdlib.h>

int test_begin(TestContext* context, const char* name, const char* log_path)
{
    if (context == NULL || name == NULL || log_path == NULL) return 0;
    context->name = name;
    context->failures = 0;
    context->log = fopen(log_path, "w");
    if (context->log == NULL)
    {
        fprintf(stderr, "FAIL %s: could not open %s\n", name, log_path);
        return 0;
    }
    hal_init();
    if (os_init() != OS_STATUS_OK)
    {
        fprintf(context->log, "FAIL os_init\n");
        fclose(context->log);
        context->log = NULL;
        return 0;
    }
    test_log(context, "START %s", name);
    return 1;
}

int test_finish(TestContext* context)
{
    int result;
    if (context == NULL) return 1;
    result = context->failures == 0 ? 0 : 1;
    test_log(context, "%s %s", result == 0 ? "PASS" : "FAIL", context->name);
    os_shutdown();
    if (context->log != NULL)
    {
        fclose(context->log);
        context->log = NULL;
    }
    return result;
}

void test_log(TestContext* context, const char* format, ...)
{
    va_list arguments;
    if (context == NULL || format == NULL) return;
    va_start(arguments, format);
    vfprintf(stdout, format, arguments);
    fputc('\n', stdout);
    va_end(arguments);
    if (context->log != NULL)
    {
        va_start(arguments, format);
        vfprintf(context->log, format, arguments);
        fputc('\n', context->log);
        fflush(context->log);
        va_end(arguments);
    }
}

int test_expect(TestContext* context, int condition, const char* message)
{
    if (condition)
    {
        test_log(context, "PASS %s", message);
        return 1;
    }
    if (context != NULL) ++context->failures;
    test_log(context, "FAIL %s", message);
    return 0;
}

int test_load_binary(TestContext* context, const char* path, TestBinary* binary)
{
    FILE* file;
    long size;
    if (path == NULL || binary == NULL) return test_expect(context, 0, "binary arguments");
    binary->bytes = NULL;
    binary->size = 0U;
    file = fopen(path, "rb");
    if (file == NULL) return test_expect(context, 0, "open WASM file");
    if (fseek(file, 0L, SEEK_END) != 0)
    {
        fclose(file);
        return test_expect(context, 0, "seek WASM file");
    }
    size = ftell(file);
    if (size <= 0L || (unsigned long)size > 0xFFFFFFFFUL)
    {
        fclose(file);
        return test_expect(context, 0, "valid WASM size");
    }
    rewind(file);
    binary->bytes = (uint8_t*)malloc((size_t)size);
    if (binary->bytes == NULL)
    {
        fclose(file);
        return test_expect(context, 0, "allocate WASM buffer");
    }
    if (fread(binary->bytes, 1U, (size_t)size, file) != (size_t)size)
    {
        fclose(file);
        test_free_binary(binary);
        return test_expect(context, 0, "read WASM file");
    }
    fclose(file);
    binary->size = (uint32_t)size;
    test_log(context, "WASM bytes=%u", binary->size);
    return 1;
}

void test_free_binary(TestBinary* binary)
{
    if (binary == NULL) return;
    free(binary->bytes);
    binary->bytes = NULL;
    binary->size = 0U;
}

int test_schedule_until_dead(TestContext* context, OsTaskHandle task, uint32_t max_iterations)
{
    uint32_t iteration;
    for (iteration = 0U; iteration < max_iterations; ++iteration)
    {
        OsStatus status = os_schedule();
        if (status != OS_STATUS_OK)
        {
            test_log(context, "scheduler status=%u iteration=%u", (uint32_t)status, iteration + 1U);
            return 0;
        }
        if (os_task_get_state(task) == OS_TASK_DEAD)
        {
            test_log(context, "scheduler iterations=%u", iteration + 1U);
            return 1;
        }
    }
    return 0;
}

int test_expect_clean_os(TestContext* context)
{
    return test_expect(context,
        os_get_task_count() == 0U &&
        os_get_ready_task_count() == 0U &&
        os_get_waiting_task_count() == 0U &&
        os_task_get_current() == NULL,
        "clean OS state");
}
