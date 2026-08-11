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

static int run_group(IM3Environment environment, WasmBinary * app,
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
           "library mutable data remains in the shared memory link group");

    m3_FreeRuntime(runtime);
    return 1;
}

static int call_context_no_args(IM3DylinkContext context, const char * name,
                                uint32_t expected)
{
    IM3Module module = m3_DylinkGetContextModule(context);
    IM3Function function = NULL;
    uint32_t value = UINT32_MAX;
    M3Result result = m3_DylinkActivateContext(context);
    if (!result)
        result = m3_FindFunctionInModule(&function, module, name);
    if (!result)
        result = m3_CallV(function);
    if (!result)
        result = m3_GetResultsV(function, &value);
    return result == m3Err_none && value == expected;
}

static void run_dynamic_group(IM3Environment environment, WasmBinary * app,
                              ResolverContext * resolver)
{
    IM3Runtime runtime = m3_NewRuntime(environment, 64U * 1024U, NULL);
    IM3Module firstModule = NULL;
    IM3Module secondModule = NULL;
    IM3Module thirdModule = NULL;
    IM3DylinkContext first = NULL;
    IM3DylinkContext second = NULL;
    IM3DylinkContext third = NULL;
    M3DylinkProgram program;
    M3DylinkOptions options = {resolver, resolve_library, NULL, 16U * 1024U};
    M3Result result = runtime != NULL
        ? m3_ParseModule(environment, &firstModule, app->bytes, app->size)
        : m3Err_mallocFailed;

    expect(runtime != NULL && result == m3Err_none && firstModule != NULL,
           "prepare dynamically extendable dylink group");
    if (!runtime || result || !firstModule)
    {
        if (runtime)
            m3_FreeRuntime(runtime);
        return;
    }

    memset(&program, 0, sizeof(program));
    program.name = "dynamic-a";
    program.module = firstModule;
    program.nativeStackSize = 64U * 1024U;
    program.linearStackSize = 16U * 1024U;
    result = m3_DylinkLoadGroup(runtime, &program, 1U, &options, &first);
    expect(result == m3Err_none && first != NULL &&
               m3_DylinkGetProgramCount(runtime) == 1U,
           "create group with one dynamic program");
    if (result || !first)
    {
        if (firstModule != NULL && firstModule->runtime == runtime)
            firstModule = NULL;
        m3_FreeRuntime(runtime);
        if (firstModule != NULL)
            m3_FreeModule(firstModule);
        return;
    }
    firstModule = NULL;

    result = m3_ParseModule(environment, &secondModule, app->bytes, app->size);
    if (!result)
    {
        memset(&program, 0, sizeof(program));
        program.name = "dynamic-b";
        program.module = secondModule;
        program.nativeStackSize = 64U * 1024U;
        program.linearStackSize = 16U * 1024U;
        result = m3_DylinkAddProgram(runtime, &program, &options, &second);
    }
    if (result == m3Err_none)
        secondModule = NULL;
    expect(result == m3Err_none && second != NULL &&
               m3_DylinkGetProgramCount(runtime) == 2U,
           "add a program after the group is running");
    expect(first != NULL && call_context_no_args(first, "app_counter", 2U),
           "run first dynamically linked program");
    expect(second != NULL && call_context_no_args(second, "app_counter", 4U),
           "run second program against the same resident library");

    result = first != NULL ? m3_DylinkRemoveProgram(first)
                           : m3Err_dylinkUnsupported;
    first = NULL;
    expect(result == m3Err_none && m3_DylinkGetProgramCount(runtime) == 1U,
           "remove one program without unloading its dependency");
    expect(second != NULL && call_context_no_args(second, "app_counter", 6U),
           "remaining context survives removal of the first program");
    result = second != NULL ? m3_DylinkRemoveProgram(second)
                            : m3Err_dylinkUnsupported;
    second = NULL;
    expect(result == m3Err_none && m3_DylinkGetProgramCount(runtime) == 0U,
           "group remains alive with no application contexts");

    result = m3_ParseModule(environment, &thirdModule, app->bytes, app->size);
    if (!result)
    {
        memset(&program, 0, sizeof(program));
        program.name = "dynamic-c";
        program.module = thirdModule;
        program.nativeStackSize = 64U * 1024U;
        program.linearStackSize = 16U * 1024U;
        result = m3_DylinkAddProgram(runtime, &program, &options, &third);
    }
    if (result == m3Err_none)
        thirdModule = NULL;
    expect(result == m3Err_none && third != NULL &&
               m3_DylinkGetProgramCount(runtime) == 1U,
           "add a program after the group became empty");
    expect(third != NULL && call_context_no_args(third, "app_counter", 8U),
           "resident library state survives close and reopen");
    if (third != NULL)
        expect(m3_DylinkRemoveProgram(third) == m3Err_none,
               "remove the reopened program");
    if (secondModule != NULL)
        m3_FreeModule(secondModule);
    if (thirdModule != NULL)
        m3_FreeModule(thirdModule);
    m3_FreeRuntime(runtime);
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

static void run_os_resident_group(WasmBinary * app, WasmBinary * library)
{
    OsWasmLibraryHandle registered = NULL;
    OsWasmLibraryHandle rejected = NULL;
    OsTaskHandle first = NULL;
    OsTaskHandle second = NULL;
    OsTaskHandle third = NULL;
    OsTaskHandle cancelled = NULL;
    OsTaskHandle reopened = NULL;
    OsValue firstValue = {0};
    OsValue secondValue = {0};
    OsValue thirdValue = {0};
    OsValue reopenedValue = {0};
    OsStatus status;
    uint32_t snapshotSize = 0U;
    uint32_t iteration;
    uint32_t firstResult = 0U;
    uint32_t secondResult = 0U;

    expect(os_init() == OS_STATUS_OK, "initialize RTOS resident link group");
    status = os_wasm_library_register(&registered, "libdylink0.so",
                                      library->bytes, library->size);
    expect(status == OS_STATUS_OK && registered != NULL,
           "register one resident Wasm library");
    expect(os_wasm_library_get_count() == 1U &&
               os_wasm_library_find("libdylink0.so") == registered &&
               os_wasm_library_get_id(registered) != 0U &&
               strcmp(os_wasm_library_get_name(registered),
                      "libdylink0.so") == 0,
           "query resident library registry");

    status = os_task_create(&first, app->bytes, app->size, "app_counter",
                            "resident-a", 64U * 1024U,
                            OS_TASK_PRIORITY_NORMAL);
    expect(status == OS_STATUS_OK && first != NULL,
           "create first PIE task");
    if (first != NULL)
    {
        expect(os_task_get_snapshot_size(first, &snapshotSize) ==
                   OS_STATUS_UNSUPPORTED,
               "reject task-local snapshot of shared library state");
    }

    status = os_wasm_link_group_seal();
    expect(status == OS_STATUS_OK && os_wasm_link_group_is_sealed(),
           "seal and place the resident link group");
    status = os_wasm_library_register(&rejected, "late.so",
                                      library->bytes, library->size);
    expect(status == OS_STATUS_BUSY && rejected == NULL,
           "reject library registration after placement");
    status = os_task_create(&second, app->bytes, app->size, "app_counter",
                            "resident-b", 64U * 1024U,
                            OS_TASK_PRIORITY_NORMAL);
    expect(status == OS_STATUS_OK && second != NULL,
           "create a PIE task after the scheduler group is sealed");

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
               "fuel switches between independent program contexts");
        expect(os_task_get_exit_reason(first) == OS_TASK_EXIT_RETURNED &&
                   os_task_get_exit_reason(second) == OS_TASK_EXIT_RETURNED,
               "both linked tasks return normally");
        status = os_task_get_return_value(first, &firstValue);
        if (status == OS_STATUS_OK && firstValue.type == OS_VALUE_TYPE_I32)
            firstResult = firstValue.value.i32;
        status = os_task_get_return_value(second, &secondValue);
        if (status == OS_STATUS_OK && secondValue.type == OS_VALUE_TYPE_I32)
            secondResult = secondValue.value.i32;
        expect(firstValue.type == OS_VALUE_TYPE_I32 &&
                   secondValue.type == OS_VALUE_TYPE_I32 &&
                   ((firstResult == 4U &&
                     (secondResult == 2U || secondResult == 3U)) ||
                    (secondResult == 4U &&
                     (firstResult == 2U || firstResult == 3U))),
               "both apps mutate one shared resident-library counter");
    }

    status = os_task_create(&third, app->bytes, app->size, "app_counter",
                            "resident-c", 64U * 1024U,
                            OS_TASK_PRIORITY_NORMAL);
    expect(status == OS_STATUS_OK && third != NULL,
           "open another application after previous contexts closed");
    for (iteration = 0U;
         status == OS_STATUS_OK && iteration < 2000U && third != NULL &&
             os_task_get_state(third) != OS_TASK_DEAD;
         ++iteration)
    {
        status = os_schedule();
    }
    expect(status == OS_STATUS_OK && third != NULL &&
               os_task_get_state(third) == OS_TASK_DEAD,
           "run the reopened link group application");
    if (third != NULL)
    {
        status = os_task_get_return_value(third, &thirdValue);
        expect(status == OS_STATUS_OK &&
                   thirdValue.type == OS_VALUE_TYPE_I32 &&
                   thirdValue.value.i32 == 6U,
               "resident library state survives all applications closing");
    }

    status = os_task_create(&cancelled, app->bytes, app->size,
                            "app_work_only", "resident-cancelled",
                            64U * 1024U, OS_TASK_PRIORITY_NORMAL);
    expect(status == OS_STATUS_OK && cancelled != NULL,
           "create an application that will be cancelled mid-slice");
    if (status == OS_STATUS_OK && cancelled != NULL)
        status = os_schedule();
    expect(status == OS_STATUS_OK && cancelled != NULL &&
               os_task_get_state(cancelled) == OS_TASK_READY,
           "suspend the cancellable application on fuel exhaustion");
    expect(cancelled != NULL && os_task_delete(cancelled) == OS_STATUS_OK,
           "delete a live dynamically linked task");
    cancelled = NULL;

    status = os_task_create(&reopened, app->bytes, app->size, "app_counter",
                            "resident-reopened", 64U * 1024U,
                            OS_TASK_PRIORITY_NORMAL);
    expect(status == OS_STATUS_OK && reopened != NULL,
           "reuse released program resources for another task");
    for (iteration = 0U;
         status == OS_STATUS_OK && iteration < 2000U && reopened != NULL &&
             os_task_get_state(reopened) != OS_TASK_DEAD;
         ++iteration)
    {
        status = os_schedule();
    }
    expect(status == OS_STATUS_OK && reopened != NULL &&
               os_task_get_state(reopened) == OS_TASK_DEAD,
           "run a task after deleting a suspended context");
    if (reopened != NULL)
    {
        status = os_task_get_return_value(reopened, &reopenedValue);
        expect(status == OS_STATUS_OK &&
                   reopenedValue.type == OS_VALUE_TYPE_I32 &&
                   reopenedValue.value.i32 == 8U,
               "deleting an application keeps shared library state intact");
    }

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
    run_group(environment, &app, &appM3C, &resolver);
    run_dynamic_group(environment, &app, &resolver);
    run_os_resident_group(&app, &library);

    expect(write_m3c(environment, &app, &appM3C),
           "write app .m3c image with unresolved Wasm imports");
    expect(write_m3c(environment, &library, &libraryM3C),
           "write library .m3c image with dylink relocations");
    if (appM3C.size && libraryM3C.size)
    {
        resolver.useM3C = 1;
        run_group(environment, &app, &appM3C, &resolver);
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
