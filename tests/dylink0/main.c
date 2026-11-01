#include "os.h"
#include "wasm3/source/m3_dylink.h"
#include "wasm3/source/m3_env.h"
#include "wasm3/source/m3_m3c.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct WasmBinary
{
    uint8_t * bytes;
    uint32_t size;
}
WasmBinary;

typedef struct MemoryStorage
{
    uint8_t * bytes;
    size_t size;
    size_t capacity;
}
MemoryStorage;

typedef struct ResolverContext
{
    IM3Environment environment;
    WasmBinary library;
    MemoryStorage * libraryM3C;
    int useM3C;
}
ResolverContext;

static FILE * g_log;
static int g_failures;

static void log_message(const char * format, ...)
{
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    printf("\n");
    va_end(args);

    va_start(args, format);
    vfprintf(g_log, format, args);
    fprintf(g_log, "\n");
    fflush(g_log);
    va_end(args);
}

static void expect(int condition, const char * message)
{
    log_message("%s %s", condition ? "PASS" : "FAIL", message);
    if (!condition)
        ++g_failures;
}

static int load_binary(const char * path, WasmBinary * binary)
{
    FILE * file = fopen(path, "rb");
    long size;
    binary->bytes = NULL;
    binary->size = 0;
    if (!file || fseek(file, 0, SEEK_END) != 0
        || (size = ftell(file)) <= 0 || (unsigned long) size > UINT32_MAX
        || fseek(file, 0, SEEK_SET) != 0)
    {
        if (file)
            fclose(file);
        return 0;
    }
    binary->bytes = (uint8_t *) malloc((size_t) size);
    if (!binary->bytes
        || fread(binary->bytes, 1, (size_t) size, file) != (size_t) size)
    {
        free(binary->bytes);
        binary->bytes = NULL;
        fclose(file);
        return 0;
    }
    fclose(file);
    binary->size = (uint32_t) size;
    return 1;
}

static M3Result storage_read(void * context, uint64_t offset,
                             void * data, uint32_t size)
{
    MemoryStorage * storage = (MemoryStorage *) context;
    if (offset > storage->size || size > storage->size - (size_t) offset)
        return "dylink test storage read overflow";
    memcpy(data, storage->bytes + (size_t) offset, size);
    return m3Err_none;
}

static M3Result storage_write(void * context, uint64_t offset,
                              const void * data, uint32_t size)
{
    MemoryStorage * storage = (MemoryStorage *) context;
    size_t end;
    if (offset > SIZE_MAX || size > SIZE_MAX - (size_t) offset)
        return "dylink test storage write overflow";
    end = (size_t) offset + size;
    if (end > storage->capacity)
    {
        size_t capacity = storage->capacity ? storage->capacity : 512;
        uint8_t * bytes;
        while (capacity < end)
            capacity *= 2;
        bytes = (uint8_t *) realloc(storage->bytes, capacity);
        if (!bytes)
            return m3Err_mallocFailed;
        storage->bytes = bytes;
        storage->capacity = capacity;
    }
    memcpy(storage->bytes + (size_t) offset, data, size);
    if (end > storage->size)
        storage->size = end;
    return m3Err_none;
}

static M3CStorage make_storage(MemoryStorage * memory)
{
    M3CStorage storage = {memory, storage_read, storage_write, NULL};
    return storage;
}

static M3Result resolve_library(void * context, const char * name,
                                IM3Module * outModule)
{
    ResolverContext * resolver = (ResolverContext *) context;
    if (!name || !strstr(name, "libdylink0.so"))
        return "unexpected dylink dependency";
    if (resolver->useM3C)
    {
        M3CStorage storage = make_storage(resolver->libraryM3C);
        return m3_ParseM3C(resolver->environment, outModule, &storage, 0);
    }
    return m3_ParseModule(resolver->environment, outModule,
                          resolver->library.bytes, resolver->library.size);
}

static int call_no_args(IM3Runtime runtime, const char * name,
                        uint32_t expected)
{
    IM3Function function = NULL;
    uint32_t value = UINT32_MAX;
    M3Result result = m3_FindFunction(&function, runtime, name);
    if (!result)
        result = m3_CallV(function);
    if (!result)
        result = m3_GetResultsV(function, &value);
    return result == m3Err_none && value == expected;
}

static int run_program(IM3Environment environment, WasmBinary * app,
                     MemoryStorage * appM3C, ResolverContext * resolver)
{
    IM3Runtime runtime = m3_NewRuntime(environment, 64U * 1024U, NULL);
    IM3Module module = NULL;
    IM3Function function = NULL;
    M3Result result;
    uint32_t value = UINT32_MAX;
    uint32_t attempts;

    expect(runtime != NULL, "create dylink runtime");
    if (!runtime)
        return 0;
    if (resolver->useM3C)
    {
        M3CStorage storage = make_storage(appM3C);
        result = m3_ParseM3C(environment, &module, &storage, 0);
    }
    else result = m3_ParseModule(environment, &module, app->bytes, app->size);
    expect(result == m3Err_none && module != NULL,
           resolver->useM3C ? "parse app from .m3c" : "parse app Wasm");
    if (result || !module)
    {
        m3_FreeRuntime(runtime);
        return 0;
    }

    M3DylinkOptions options = {resolver, resolve_library, NULL, 16U * 1024U};
    result = m3_DylinkLoad(runtime, module, "dylink0-app", &options);
    expect(result == m3Err_none,
           resolver->useM3C ? "link .m3c app and library" : "link Wasm app and library");
    if (result)
    {
        m3_FreeRuntime(runtime);
        return 0;
    }

    result = m3_FindFunction(&function, runtime, "app_main");
    expect(result == m3Err_none && function != NULL,
           "find app function that imports library code");
    if (!result && function)
    {
        m3_SetFuel(runtime, 1);
        result = m3_CallV(function, (uint32_t) 5);
        for (attempts = 0; attempts < 2000 && result == m3Err_fuelExhausted;
             ++attempts)
        {
            m3_AddFuel(runtime, 1);
            result = m3_Resume(runtime);
        }
        if (!result)
            result = m3_GetResultsV(function, &value);
    }
    expect(result == m3Err_none && value == 42,
           "fuel resume crosses a direct Wasm library call");
    m3_DisableFuel(runtime);

    result = m3_FindFunction(&function, runtime, "app_name_char");
    if (!result)
        result = m3_CallV(function, (uint32_t) 1);
    value = UINT32_MAX;
    if (!result)
        result = m3_GetResultsV(function, &value);
    expect(result == m3Err_none && value == (uint32_t) 'V',
           "library returns const char pointer through shared memory");
    expect(call_no_args(runtime, "app_struct", 41),
           "app passes a struct pointer to the library");
    expect(call_no_args(runtime, "app_callback", 17),
           "library calls an app function pointer through the shared table");
    expect(call_no_args(runtime, "app_counter", 2),
           "library keeps its mutable data between calls");

    m3_FreeRuntime(runtime);
    return 1;
}

static int write_m3c(IM3Environment environment, WasmBinary * binary,
                     MemoryStorage * output)
{
    IM3Module module = NULL;
    M3CStorage storage = make_storage(output);
    M3Result result = m3_ParseModule(environment, &module, binary->bytes,
                                     binary->size);
    if (!result)
        result = m3_WriteM3C(module, &storage, 0, NULL);
    if (module)
        m3_FreeModule(module);
    return result == m3Err_none;
}

/* Runs task until it dies and returns its i32 result, or 0 on failure. */
static uint32_t run_task_to_result(OsTaskHandle task)
{
    OsValue value = {0};
    OsStatus status = OS_STATUS_OK;
    uint32_t iteration;

    for (iteration = 0U;
         status == OS_STATUS_OK && iteration < 2000U && task != NULL &&
             os_task_get_state(task) != OS_TASK_DEAD;
         ++iteration)
    {
        status = os_schedule();
    }
    if (task == NULL || os_task_get_state(task) != OS_TASK_DEAD ||
        os_task_get_return_value(task, &value) != OS_STATUS_OK ||
        value.type != OS_VALUE_TYPE_I32)
        return 0U;
    return value.value.i32;
}

static void run_os_processes(WasmBinary * app, WasmBinary * library)
{
    OsWasmLibraryHandle registered = NULL;
    OsWasmLibraryHandle late = NULL;
    OsTaskHandle first = NULL;
    OsTaskHandle second = NULL;
    OsTaskHandle third = NULL;
    OsTaskHandle cancelled = NULL;
    OsTaskHandle missing = NULL;
    OsValue firstValue = {0};
    OsValue secondValue = {0};
    OsStatus status;
    uint32_t snapshotSize = 0U;
    uint32_t iteration;

    expect(os_init() == OS_STATUS_OK, "initialize RTOS for linked processes");
    status = os_wasm_library_register(&registered, "libdylink0.so",
                                      library->bytes, library->size);
    expect(status == OS_STATUS_OK && registered != NULL,
           "register one Wasm library");
    expect(os_wasm_library_get_count() == 1U &&
               os_wasm_library_find("libdylink0.so") == registered &&
               os_wasm_library_get_id(registered) != 0U &&
               strcmp(os_wasm_library_get_name(registered),
                      "libdylink0.so") == 0,
           "query library registry");

    status = os_task_create(&first, app->bytes, app->size, "app_counter",
                            "linked-a", 64U * 1024U,
                            OS_TASK_PRIORITY_NORMAL);
    expect(status == OS_STATUS_OK && first != NULL,
           "create first PIE task");
    if (first != NULL)
    {
        expect(os_task_get_snapshot_size(first, &snapshotSize) ==
                   OS_STATUS_UNSUPPORTED,
               "snapshots of dynamically linked tasks are not supported yet");
    }
    status = os_task_create(&second, app->bytes, app->size, "app_counter",
                            "linked-b", 64U * 1024U,
                            OS_TASK_PRIORITY_NORMAL);
    expect(status == OS_STATUS_OK && second != NULL,
           "create second PIE task");

    status = os_wasm_library_register(&late, "late.so",
                                      library->bytes, library->size);
    expect(status == OS_STATUS_OK && late != NULL &&
               os_wasm_library_get_count() == 2U,
           "register a library while linked tasks exist");

    status = OS_STATUS_OK;
    for (iteration = 0U;
         status == OS_STATUS_OK && iteration < 2000U &&
             first != NULL && second != NULL &&
             (os_task_get_state(first) != OS_TASK_DEAD ||
              os_task_get_state(second) != OS_TASK_DEAD);
         ++iteration)
    {
        status = os_schedule();
    }
    expect(status == OS_STATUS_OK && first != NULL && second != NULL &&
               os_task_get_state(first) == OS_TASK_DEAD &&
               os_task_get_state(second) == OS_TASK_DEAD,
           "run both linked tasks to completion");
    if (first != NULL && second != NULL)
    {
        expect(os_task_get_run_count(first) > 1U &&
                   os_task_get_run_count(second) > 1U,
               "fuel switches between linked processes");
        expect(os_task_get_exit_reason(first) == OS_TASK_EXIT_RETURNED &&
                   os_task_get_exit_reason(second) == OS_TASK_EXIT_RETURNED,
               "both linked tasks return normally");
        expect(os_task_get_return_value(first, &firstValue) == OS_STATUS_OK &&
                   os_task_get_return_value(second, &secondValue) == OS_STATUS_OK &&
                   firstValue.type == OS_VALUE_TYPE_I32 &&
                   secondValue.type == OS_VALUE_TYPE_I32 &&
                   firstValue.value.i32 == 2U && secondValue.value.i32 == 2U,
               "each process has its own library data");
    }

    status = os_task_create(&third, app->bytes, app->size, "app_counter",
                            "linked-c", 64U * 1024U,
                            OS_TASK_PRIORITY_NORMAL);
    expect(status == OS_STATUS_OK && third != NULL,
           "create another linked task later");
    expect(run_task_to_result(third) == 2U,
           "a new process starts with fresh library data");

    status = os_task_create(&cancelled, app->bytes, app->size,
                            "app_work_only", "linked-cancelled",
                            64U * 1024U, OS_TASK_PRIORITY_NORMAL);
    expect(status == OS_STATUS_OK && cancelled != NULL,
           "create a linked task that will be cancelled mid-slice");
    if (status == OS_STATUS_OK && cancelled != NULL)
        status = os_schedule();
    expect(status == OS_STATUS_OK && cancelled != NULL &&
               os_task_get_state(cancelled) == OS_TASK_READY,
           "suspend the linked task on fuel exhaustion");
    expect(cancelled != NULL && os_task_delete(cancelled) == OS_STATUS_OK,
           "delete a live dynamically linked task");
    os_shutdown();

    /* After shutdown no library is registered, so the program cannot load. */
    expect(os_init() == OS_STATUS_OK, "reinitialize RTOS without libraries");
    status = os_task_create(&missing, app->bytes, app->size, "app_counter",
                            "linked-missing", 64U * 1024U,
                            OS_TASK_PRIORITY_NORMAL);
    expect(status != OS_STATUS_OK && missing == NULL,
           "a program whose library is missing fails to load");
    os_shutdown();
}

int main(void)
{
    WasmBinary app = {0};
    WasmBinary library = {0};
    MemoryStorage appM3C = {0};
    MemoryStorage libraryM3C = {0};
    IM3Environment environment = NULL;
    ResolverContext resolver;

    g_log = fopen("dylink0.log", "w");
    if (!g_log)
        return 1;
    expect(load_binary("dylink0.wasm", &app), "load PIE app artifact");
    expect(load_binary("libdylink0.so", &library), "load shared library artifact");
    environment = m3_NewEnvironment();
    expect(environment != NULL, "create dylink environment");
    if (!app.bytes || !library.bytes || !environment)
        goto cleanup;

    resolver.environment = environment;
    resolver.library = library;
    resolver.libraryM3C = &libraryM3C;
    resolver.useM3C = 0;
    run_program(environment, &app, &appM3C, &resolver);
    run_os_processes(&app, &library);

    expect(write_m3c(environment, &app, &appM3C),
           "write app .m3c image with unresolved Wasm imports");
    expect(write_m3c(environment, &library, &libraryM3C),
           "write library .m3c image with dylink relocations");
    if (appM3C.size && libraryM3C.size)
    {
        resolver.useM3C = 1;
        run_program(environment, &app, &appM3C, &resolver);
    }

cleanup:
    if (environment)
        m3_FreeEnvironment(environment);
    free(app.bytes);
    free(library.bytes);
    free(appM3C.bytes);
    free(libraryM3C.bytes);
    expect(g_failures == 0, "dylink0");
    fclose(g_log);
    return g_failures ? 1 : 0;
}
