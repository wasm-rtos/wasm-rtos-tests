#include "os.h"
#include "hal.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct WasmBinary
{
    uint8_t* bytes;
    uint32_t size;
} WasmBinary;

typedef struct FileLibrarySource
{
    const char* path;
    uint8_t* bytes;
    uint32_t size;
    uint32_t acquire_count;
    uint32_t release_count;
    uint8_t release_mismatch;
} FileLibrarySource;

static FILE* g_log = NULL;
static int g_failures = 0;
static uint32_t g_phase = 0U;

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

static int load_file(const char* path, WasmBinary* binary)
{
    FILE* file = NULL;
    long size = 0L;

    if (path == NULL || binary == NULL)
    {
        return 0;
    }

    binary->bytes = NULL;
    binary->size = 0U;
    file = fopen(path, "rb");
    if (file == NULL ||
        fseek(file, 0L, SEEK_END) != 0 ||
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
        fclose(file);
        free(binary->bytes);
        binary->bytes = NULL;
        return 0;
    }

    fclose(file);
    binary->size = (uint32_t)size;
    return 1;
}

static OsStatus acquire_file_library(
    void* context,
    const uint8_t** out_wasm_bytes,
    uint32_t* out_wasm_size
)
{
    FileLibrarySource* source = (FileLibrarySource*)context;
    WasmBinary binary = {0};

    if (source == NULL || out_wasm_bytes == NULL ||
        out_wasm_size == NULL || source->bytes != NULL)
    {
        return OS_STATUS_INVALID_ARGUMENT;
    }

    if (!load_file(source->path, &binary))
    {
        return OS_STATUS_ERROR;
    }

    source->bytes = binary.bytes;
    source->size = binary.size;
    ++source->acquire_count;
    *out_wasm_bytes = source->bytes;
    *out_wasm_size = source->size;
    return OS_STATUS_OK;
}

static void release_file_library(
    void* context,
    const uint8_t* wasm_bytes,
    uint32_t wasm_size
)
{
    FileLibrarySource* source = (FileLibrarySource*)context;

    if (source == NULL)
    {
        return;
    }

    ++source->release_count;
    if (wasm_bytes != source->bytes || wasm_size != source->size)
    {
        source->release_mismatch = 1U;
    }

    free(source->bytes);
    source->bytes = NULL;
    source->size = 0U;
}

static m3ApiRawFunction(record_phase)
{
    m3ApiGetArg(uint32_t, phase);
    (void)runtime;
    (void)_ctx;
    (void)_mem;

    g_phase = phase;
    m3ApiSuccess();
}

static int schedule_until_phase(uint32_t phase, uint32_t max_slices)
{
    uint32_t slice = 0U;

    for (slice = 0U; slice < max_slices; ++slice)
    {
        if (os_schedule() != OS_STATUS_OK)
        {
            return 0;
        }
        if (g_phase >= phase)
        {
            return 1;
        }
    }

    return 0;
}

static int schedule_until_dead(OsTaskHandle task, uint32_t max_slices)
{
    uint32_t slice = 0U;

    for (slice = 0U; slice < max_slices; ++slice)
    {
        if (os_task_get_state(task) == OS_TASK_DEAD)
        {
            return 1;
        }
        if (os_schedule() != OS_STATUS_OK)
        {
            return 0;
        }
    }

    return os_task_get_state(task) == OS_TASK_DEAD;
}

int main(void)
{
    WasmBinary app = {0};
    FileLibrarySource library_file =
    {
        "math_dll.wasm",
        NULL,
        0U,
        0U,
        0U,
        0U
    };
    OsWasmLibrarySource source = {0};
    OsWasmLibraryHandle library = NULL;
    OsTaskHandle task = NULL;

    g_log = fopen("wasm_dll_file.log", "w");
    if (g_log == NULL)
    {
        return 1;
    }

    hal_init();
    expect(os_init() == OS_STATUS_OK, "initialize OS");
    expect(
        os_host_import_register(
            "env",
            "host_phase",
            "v(i)",
            record_phase
        ) == OS_STATUS_OK,
        "register phase import"
    );
    expect(
        load_file("wasm_dll_file.wasm", &app),
        "load app from wasm_dll_file.wasm"
    );

    source.acquire = acquire_file_library;
    source.release = release_file_library;
    source.context = &library_file;
    source.stack_size = 8U * 1024U;
    expect(
        os_wasm_library_register(
            &library,
            "math",
            &source,
            OS_WASM_LIBRARY_EVICTABLE
        ) == OS_STATUS_OK,
        "register math_dll.wasm as module math"
    );
    expect(
        library_file.acquire_count == 1U &&
        library_file.release_count == 1U &&
        library_file.bytes == NULL,
        "registration validates and closes the DLL file"
    );

    if (app.bytes != NULL && library != NULL)
    {
        expect(
            os_task_create(
                &task,
                app.bytes,
                app.size,
                "app_main",
                "wasm_dll_file",
                64U * 1024U,
                OS_TASK_PRIORITY_NORMAL
            ) == OS_STATUS_OK,
            "create app.wasm with math.add import"
        );
        expect(
            task != NULL &&
            library_file.acquire_count == 1U &&
            !os_wasm_library_is_resident(task, library),
            "task creation does not reopen math_dll.wasm"
        );
        expect(
            task != NULL &&
            os_schedule() == OS_STATUS_OK &&
            library_file.acquire_count == 1U,
            "first DLL import yields before file acquisition"
        );
        expect(
            task != NULL && schedule_until_phase(1U, 100U),
            "app.wasm receives 42 from math_dll.wasm"
        );
        expect(
            library_file.acquire_count == 2U &&
            library_file.release_count == 1U &&
            os_wasm_library_is_resident(task, library),
            "first call lazily opens and keeps the DLL resident"
        );
        expect(
            os_wasm_library_evict(task, library) == OS_STATUS_OK &&
            library_file.acquire_count == 2U &&
            library_file.release_count == 2U &&
            library_file.bytes == NULL &&
            !os_wasm_library_is_resident(task, library),
            "eviction closes math_dll.wasm"
        );
        expect(
            schedule_until_phase(2U, 100U) &&
            library_file.acquire_count == 3U &&
            os_wasm_library_is_resident(task, library),
            "second app.wasm call reopens math_dll.wasm"
        );
        expect(
            schedule_until_dead(task, 20U) &&
            os_task_get_exit_code(task) == 0U,
            "app.wasm exits after both DLL calls"
        );
        expect(
            library_file.acquire_count == 3U &&
            library_file.release_count == 3U &&
            library_file.bytes == NULL &&
            !library_file.release_mismatch,
            "task exit releases the reloaded DLL file"
        );
    }

    if (task != NULL)
    {
        expect(os_task_delete(task) == OS_STATUS_OK, "delete app task");
    }
    if (library != NULL)
    {
        expect(
            os_wasm_library_unregister(library) == OS_STATUS_OK,
            "unregister math DLL"
        );
    }

    free(app.bytes);
    os_shutdown();
    hal_shutdown();
    expect(g_failures == 0, "wasm_dll_file");

    fclose(g_log);
    return g_failures == 0 ? 0 : 1;
}
