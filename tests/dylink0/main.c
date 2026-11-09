#include "os.h"
#include "vm/source/wm_dylink.h"

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

typedef struct ResolverContext
{
    WasmBinary library;
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

static WmResult resolve_library(void * context, const char * name,
                                WmModule ** outModule)
{
    ResolverContext * resolver = (ResolverContext *) context;
    (void) name;
    return wm_module_load(outModule, resolver->library.bytes,
                          resolver->library.size);
}

static WmFunc * find_function(WmInstance * instance, const char * name)
{
    WmExtern found;
    if (wm_instance_export(instance, name, (uint32_t) strlen(name), &found) ||
        found.kind != WM_EXTERN_FUNC)
        return NULL;
    return found.as.func;
}

static int call_no_args(WmRuntime * runtime, WmInstance * instance,
                        const char * name, uint32_t expected)
{
    WmFunc * function = find_function(instance, name);
    uint32_t value = UINT32_MAX;
    return function && wm_call(runtime, function, NULL, &value) == NULL &&
           value == expected;
}

static int run_program(WasmBinary * app, ResolverContext * resolver)
{
    WmRuntime * runtime = wm_runtime_new(64U * 1024U, 1024U);
    WmModule * module = NULL;
    WmDylink * process = NULL;
    WmInstance * instance = NULL;
    WmFunc * function = NULL;
    WmResult result;
    WmDylinkOptions options = {resolver, resolve_library, NULL, 16U * 1024U};
    uint32_t value = UINT32_MAX;
    uint32_t argument;
    uint32_t attempts;

    expect(runtime != NULL, "create dylink runtime");
    if (!runtime)
        return 0;
    result = wm_module_load(&module, app->bytes, app->size);
    expect(result == NULL && module != NULL && wm_module_is_dylink(module),
           "parse app Wasm");
    if (result || !module)
    {
        wm_runtime_free(runtime);
        return 0;
    }

    result = wm_dylink_load(&process, module, "dylink0-app", runtime, &options);
    expect(result == NULL, "link Wasm app and library");
    if (result)
        goto done;
    instance = wm_dylink_instance(process);

    function = find_function(instance, "app_main");
    expect(function != NULL, "find app function that imports library code");
    result = "no app_main";
    if (function)
    {
        argument = 5;
        wm_set_fuel(runtime, 1);
        result = wm_call(runtime, function, &argument, &value);
        for (attempts = 0; attempts < 2000 && result == wm_fuel_exhausted;
             ++attempts)
        {
            wm_set_fuel(runtime, 1);
            result = wm_resume(runtime, &value);
        }
    }
    expect(result == NULL && value == 42,
           "fuel resume crosses a direct Wasm library call");
    wm_disable_fuel(runtime);

    function = find_function(instance, "app_name_char");
    value = UINT32_MAX;
    argument = 1;
    expect(function && wm_call(runtime, function, &argument, &value) == NULL &&
               value == (uint32_t) 'V',
           "library returns const char pointer through shared memory");
    expect(call_no_args(runtime, instance, "app_struct", 41),
           "app passes a struct pointer to the library");
    expect(call_no_args(runtime, instance, "app_callback", 17),
           "library calls an app function pointer through the shared table");
    expect(call_no_args(runtime, instance, "app_counter", 2),
           "library keeps its mutable data between calls");

done:
    wm_dylink_free(process);
    wm_module_free(module);
    wm_runtime_free(runtime);
    return result == NULL;
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
    ResolverContext resolver;

    g_log = fopen("dylink0.log", "w");
    if (!g_log)
        return 1;
    expect(load_binary("dylink0.wasm", &app), "load PIE app artifact");
    expect(load_binary("libdylink0.so", &library), "load shared library artifact");
    if (!app.bytes || !library.bytes)
        goto cleanup;

    resolver.library = library;
    run_program(&app, &resolver);
    run_os_processes(&app, &library);

cleanup:
    free(app.bytes);
    free(library.bytes);
    expect(g_failures == 0, "dylink0");
    fclose(g_log);
    return g_failures ? 1 : 0;
}
