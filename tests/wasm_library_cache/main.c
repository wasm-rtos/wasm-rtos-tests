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

typedef struct TestLibrarySource
{
    const uint8_t* bytes;
    uint32_t size;
    uint32_t acquire_count;
    uint32_t release_count;
    uint8_t release_mismatch;
} TestLibrarySource;

static const uint8_t math_library[] =
{
    0x00U, 0x61U, 0x73U, 0x6dU, 0x01U, 0x00U, 0x00U, 0x00U,
    0x01U, 0x19U, 0x04U, 0x60U, 0x02U, 0x7fU, 0x7fU, 0x01U,
    0x7fU, 0x60U, 0x02U, 0x7eU, 0x7eU, 0x01U, 0x7eU, 0x60U,
    0x02U, 0x7dU, 0x7dU, 0x01U, 0x7dU, 0x60U, 0x02U, 0x7cU,
    0x7cU, 0x01U, 0x7cU, 0x03U, 0x05U, 0x04U, 0x00U, 0x01U,
    0x02U, 0x03U, 0x07U, 0x2bU, 0x04U, 0x03U, 0x61U, 0x64U,
    0x64U, 0x00U, 0x00U, 0x08U, 0x77U, 0x69U, 0x64U, 0x65U,
    0x5fU, 0x61U, 0x64U, 0x64U, 0x00U, 0x01U, 0x09U, 0x66U,
    0x6cU, 0x6fU, 0x61U, 0x74U, 0x5fU, 0x61U, 0x64U, 0x64U,
    0x00U, 0x02U, 0x0aU, 0x64U, 0x6fU, 0x75U, 0x62U, 0x6cU,
    0x65U, 0x5fU, 0x61U, 0x64U, 0x64U, 0x00U, 0x03U, 0x0aU,
    0x3bU, 0x04U, 0x21U, 0x01U, 0x01U, 0x7fU, 0x02U, 0x40U,
    0x03U, 0x40U, 0x20U, 0x02U, 0x41U, 0xb0U, 0xeaU, 0x01U,
    0x4fU, 0x0dU, 0x01U, 0x20U, 0x02U, 0x41U, 0x01U, 0x6aU,
    0x21U, 0x02U, 0x0cU, 0x00U, 0x0bU, 0x0bU, 0x20U, 0x00U,
    0x20U, 0x01U, 0x6aU, 0x0bU, 0x07U, 0x00U, 0x20U, 0x00U,
    0x20U, 0x01U, 0x7cU, 0x0bU, 0x07U, 0x00U, 0x20U, 0x00U,
    0x20U, 0x01U, 0x92U, 0x0bU, 0x07U, 0x00U, 0x20U, 0x00U,
    0x20U, 0x01U, 0xa0U, 0x0bU
};

static const uint8_t scale_library[] =
{
    0x00U, 0x61U, 0x73U, 0x6dU, 0x01U, 0x00U, 0x00U, 0x00U,
    0x01U, 0x07U, 0x01U, 0x60U, 0x02U, 0x7fU, 0x7fU, 0x01U,
    0x7fU, 0x03U, 0x02U, 0x01U, 0x00U, 0x07U, 0x07U, 0x01U,
    0x03U, 0x6dU, 0x75U, 0x6cU, 0x00U, 0x00U, 0x0aU, 0x09U,
    0x01U, 0x07U, 0x00U, 0x20U, 0x00U, 0x20U, 0x01U, 0x6cU,
    0x0bU
};

static const uint8_t startup_library[] =
{
    0x00U, 0x61U, 0x73U, 0x6dU, 0x01U, 0x00U, 0x00U, 0x00U,
    0x01U, 0x08U, 0x02U, 0x60U, 0x00U, 0x00U, 0x60U, 0x00U,
    0x01U, 0x7fU, 0x03U, 0x03U, 0x02U, 0x00U, 0x01U, 0x06U,
    0x06U, 0x01U, 0x7fU, 0x01U, 0x41U, 0x00U, 0x0bU, 0x07U,
    0x09U, 0x01U, 0x05U, 0x63U, 0x68U, 0x65U, 0x63U, 0x6bU,
    0x00U, 0x01U, 0x08U, 0x01U, 0x00U, 0x0aU, 0x27U, 0x02U,
    0x20U, 0x01U, 0x01U, 0x7fU, 0x02U, 0x40U, 0x03U, 0x40U,
    0x20U, 0x00U, 0x41U, 0xb0U, 0xeaU, 0x01U, 0x4fU, 0x0dU,
    0x01U, 0x20U, 0x00U, 0x41U, 0x01U, 0x6aU, 0x21U, 0x00U,
    0x0cU, 0x00U, 0x0bU, 0x0bU, 0x41U, 0x01U, 0x24U, 0x00U,
    0x0bU, 0x04U, 0x00U, 0x23U, 0x00U, 0x0bU
};

static const uint8_t mismatched_library[] =
{
    0x00U, 0x61U, 0x73U, 0x6dU, 0x01U, 0x00U, 0x00U, 0x00U,
    0x01U, 0x07U, 0x01U, 0x60U, 0x02U, 0x7eU, 0x7eU, 0x01U,
    0x7eU, 0x03U, 0x02U, 0x01U, 0x00U, 0x07U, 0x07U, 0x01U,
    0x03U, 0x61U, 0x64U, 0x64U, 0x00U, 0x00U, 0x0aU, 0x09U,
    0x01U, 0x07U, 0x00U, 0x20U, 0x00U, 0x20U, 0x01U, 0x7cU,
    0x0bU
};

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

static int load_wasm(WasmBinary* binary)
{
    FILE* file = fopen("wasm_library_cache.wasm", "rb");
    long size = 0L;

    binary->bytes = NULL;
    binary->size = 0U;
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
        expect(0, "open wasm_library_cache.wasm");
        return 0;
    }

    binary->bytes = (uint8_t*)malloc((size_t)size);
    if (binary->bytes == NULL ||
        fread(binary->bytes, 1U, (size_t)size, file) != (size_t)size)
    {
        fclose(file);
        free(binary->bytes);
        binary->bytes = NULL;
        expect(0, "read wasm_library_cache.wasm");
        return 0;
    }

    fclose(file);
    binary->size = (uint32_t)size;
    return 1;
}

static OsStatus acquire_library(
    void* context,
    const uint8_t** out_wasm_bytes,
    uint32_t* out_wasm_size
)
{
    TestLibrarySource* source = (TestLibrarySource*)context;

    if (source == NULL || out_wasm_bytes == NULL || out_wasm_size == NULL)
    {
        return OS_STATUS_INVALID_ARGUMENT;
    }

    ++source->acquire_count;
    *out_wasm_bytes = source->bytes;
    *out_wasm_size = source->size;
    return OS_STATUS_OK;
}

static void release_library(
    void* context,
    const uint8_t* wasm_bytes,
    uint32_t wasm_size
)
{
    TestLibrarySource* source = (TestLibrarySource*)context;

    if (source == NULL)
    {
        return;
    }

    ++source->release_count;
    if (wasm_bytes != source->bytes || wasm_size != source->size)
    {
        source->release_mismatch = 1U;
    }
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
    TestLibrarySource math_source =
    {
        math_library,
        (uint32_t)sizeof(math_library),
        0U,
        0U,
        0U
    };
    TestLibrarySource scale_source =
    {
        scale_library,
        (uint32_t)sizeof(scale_library),
        0U,
        0U,
        0U
    };
    TestLibrarySource startup_source =
    {
        startup_library,
        (uint32_t)sizeof(startup_library),
        0U,
        0U,
        0U
    };
    TestLibrarySource mutable_source =
    {
        math_library,
        (uint32_t)sizeof(math_library),
        0U,
        0U,
        0U
    };
    OsWasmLibrarySource source = {0};
    OsWasmLibraryHandle math = NULL;
    OsWasmLibraryHandle scale = NULL;
    OsWasmLibraryHandle startup = NULL;
    OsWasmLibraryHandle mutable = NULL;
    OsTaskHandle task = NULL;
    OsStatus status = OS_STATUS_OK;
    uint32_t snapshot_size = 0U;

    g_log = fopen("wasm_library_cache.log", "w");
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

    source.acquire = acquire_library;
    source.release = release_library;
    source.context = &math_source;
    source.stack_size = 8U * 1024U;
    source.resident_size_bytes = 100U;
    expect(
        os_wasm_library_register(
            &math,
            "math",
            &source,
            OS_WASM_LIBRARY_EVICTABLE
        ) == OS_STATUS_OK,
        "register math library"
    );

    source.context = &scale_source;
    expect(
        os_wasm_library_register(
            &scale,
            "scale",
            &source,
            OS_WASM_LIBRARY_EVICTABLE
        ) == OS_STATUS_OK,
        "register scale library"
    );

    source.context = &startup_source;
    expect(
        os_wasm_library_register(
            &startup,
            "startup",
            &source,
            OS_WASM_LIBRARY_PINNED
        ) == OS_STATUS_OK,
        "register startup library"
    );

    source.context = &mutable_source;
    expect(
        os_wasm_library_register(
            &mutable,
            "mutable",
            &source,
            OS_WASM_LIBRARY_EVICTABLE
        ) == OS_STATUS_OK,
        "register mutable library metadata"
    );
    mutable_source.bytes = mismatched_library;
    mutable_source.size = (uint32_t)sizeof(mismatched_library);
    expect(
        math_source.acquire_count == 1U &&
        math_source.release_count == 1U &&
        scale_source.acquire_count == 1U &&
        scale_source.release_count == 1U &&
        startup_source.acquire_count == 1U &&
        startup_source.release_count == 1U &&
        mutable_source.acquire_count == 1U &&
        mutable_source.release_count == 1U,
        "registration validates then releases source bytes"
    );

    if (load_wasm(&app))
    {
        expect(
            os_wasm_library_cache_set_limit(200U) == OS_STATUS_OK,
            "set cache capacity for two libraries"
        );
        status = os_task_create(
            &task,
            app.bytes,
            app.size,
            "app_main",
            "wasm_library_cache_two",
            64U * 1024U,
            OS_TASK_PRIORITY_NORMAL
        );
        expect(
            status == OS_STATUS_OK && task != NULL,
            "create app with standard WASM imports"
        );
        expect(
            os_wasm_library_cache_get_resident_count() == 0U,
            "task creation does not load libraries"
        );
        expect(
            task != NULL && os_schedule() == OS_STATUS_OK &&
            os_wasm_library_cache_get_resident_count() == 0U,
            "first library import yields before lazy loading"
        );
        expect(
            task != NULL && schedule_until_phase(1U, 100U),
            "fuel-sliced math library call completes"
        );
        expect(
            os_wasm_library_is_resident(task, math) &&
            !os_wasm_library_is_resident(task, scale),
            "only the called math library is resident"
        );
        expect(
            os_task_get_snapshot_size(task, &snapshot_size) == OS_STATUS_BUSY,
            "snapshot rejects a task with resident libraries"
        );
        expect(
            os_wasm_library_unregister(math) == OS_STATUS_BUSY,
            "bound library cannot be unregistered"
        );
        expect(
            schedule_until_phase(2U, 20U),
            "second library call completes"
        );
        expect(
            os_wasm_library_cache_get_resident_count() == 2U &&
            os_wasm_library_cache_get_resident_size() == 200U,
            "multiple libraries remain resident when capacity allows"
        );
        expect(
            os_wasm_library_evict(task, math) == OS_STATUS_OK &&
            !os_wasm_library_is_resident(task, math) &&
            os_wasm_library_is_resident(task, scale),
            "explicit eviction fully unloads an inactive library"
        );
        expect(
            schedule_until_dead(task, 10U) &&
            os_task_get_exit_code(task) == 0U,
            "app returns correct results from both libraries"
        );
        expect(
            os_wasm_library_cache_get_resident_count() == 0U,
            "task exit unloads its remaining libraries"
        );
        expect(os_task_delete(task) == OS_STATUS_OK, "delete first task");
        task = NULL;

        g_phase = 0U;
        expect(
            os_wasm_library_cache_set_limit(100U) == OS_STATUS_OK,
            "reduce cache capacity to one library"
        );
        expect(
            os_task_create(
                &task,
                app.bytes,
                app.size,
                "app_main",
                "wasm_library_cache_lru",
                64U * 1024U,
                OS_TASK_PRIORITY_NORMAL
            ) == OS_STATUS_OK,
            "create LRU app"
        );
        expect(
            schedule_until_phase(1U, 100U) &&
            os_wasm_library_is_resident(task, math),
            "LRU app loads math first"
        );
        expect(
            schedule_until_phase(2U, 20U),
            "LRU app loads scale second"
        );
        expect(
            !os_wasm_library_is_resident(task, math) &&
            os_wasm_library_is_resident(task, scale) &&
            os_wasm_library_cache_get_resident_count() == 1U,
            "loading scale evicts least-recently-used math"
        );
        expect(
            schedule_until_dead(task, 10U) &&
            os_task_get_exit_code(task) == 0U,
            "LRU app still returns normally"
        );
        expect(os_task_delete(task) == OS_STATUS_OK, "delete LRU task");
        task = NULL;

        g_phase = 0U;
        expect(
            os_wasm_library_cache_set_limit(50U) == OS_STATUS_OK,
            "set undersized cache limit"
        );
        expect(
            os_task_create(
                &task,
                app.bytes,
                app.size,
                "app_main",
                "wasm_library_cache_full",
                64U * 1024U,
                OS_TASK_PRIORITY_NORMAL
            ) == OS_STATUS_OK,
            "create cache-full app without loading libraries"
        );
        expect(
            os_schedule() == OS_STATUS_OK,
            "cache-full app reaches first lazy import"
        );
        expect(
            os_schedule() == OS_STATUS_WASM_CACHE_FULL &&
            os_task_get_state(task) == OS_TASK_DEAD &&
            os_task_get_exit_reason(task) == OS_TASK_EXIT_WASM_ERROR,
            "undersized cache fails deterministically on first call"
        );
        expect(os_task_delete(task) == OS_STATUS_OK, "delete cache-full task");
        task = NULL;

        expect(
            os_wasm_library_cache_set_limit(100U) == OS_STATUS_OK,
            "restore one-library cache limit"
        );
        expect(
            os_task_create(
                &task,
                app.bytes,
                app.size,
                "app_main_start",
                "wasm_library_start",
                64U * 1024U,
                OS_TASK_PRIORITY_NORMAL
            ) == OS_STATUS_OK,
            "create app for a library start function"
        );
        expect(
            schedule_until_phase(3U, 100U) &&
            os_wasm_library_is_resident(task, startup),
            "library start function is fuel-sliced and runs exactly once"
        );
        expect(
            os_wasm_library_evict(task, startup) == OS_STATUS_BUSY,
            "explicit eviction preserves a pinned library"
        );
        expect(
            os_wasm_library_cache_set_limit(0U) ==
                OS_STATUS_WASM_CACHE_FULL &&
            os_wasm_library_cache_get_limit() == 100U &&
            os_wasm_library_is_resident(task, startup),
            "failed shrink leaves the pinned cache unchanged"
        );
        expect(
            schedule_until_dead(task, 10U) &&
            os_task_get_exit_code(task) == 0U,
            "start-function app returns normally"
        );
        expect(
            os_wasm_library_cache_get_resident_count() == 0U,
            "start-function task unloads its library on exit"
        );
        expect(
            os_task_delete(task) == OS_STATUS_OK,
            "delete start-function task"
        );
        task = NULL;

        expect(
            os_task_create(
                &task,
                app.bytes,
                app.size,
                "app_main_changed",
                "wasm_library_changed",
                64U * 1024U,
                OS_TASK_PRIORITY_NORMAL
            ) == OS_STATUS_OK,
            "create app against validated library metadata"
        );
        expect(
            os_schedule() == OS_STATUS_OK,
            "changed library app reaches its lazy import"
        );
        expect(
            os_schedule() == OS_STATUS_WASM_SIGNATURE_MISMATCH &&
            os_task_get_state(task) == OS_TASK_DEAD,
            "changed source signature is rejected at load time"
        );
        expect(
            os_task_delete(task) == OS_STATUS_OK,
            "delete changed-library task"
        );
        task = NULL;
    }

    expect(
        math_source.acquire_count == math_source.release_count &&
        scale_source.acquire_count == scale_source.release_count &&
        startup_source.acquire_count == startup_source.release_count &&
        mutable_source.acquire_count == mutable_source.release_count &&
        !math_source.release_mismatch &&
        !scale_source.release_mismatch &&
        !startup_source.release_mismatch &&
        !mutable_source.release_mismatch,
        "every acquired source is released with matching bytes"
    );
    expect(
        os_wasm_library_unregister(math) == OS_STATUS_OK &&
        os_wasm_library_unregister(scale) == OS_STATUS_OK &&
        os_wasm_library_unregister(startup) == OS_STATUS_OK &&
        os_wasm_library_unregister(mutable) == OS_STATUS_OK,
        "unregister unused libraries"
    );
    free(app.bytes);
    os_shutdown();
    hal_shutdown();
    expect(g_failures == 0, "wasm_library_cache");

    fclose(g_log);
    return g_failures == 0 ? 0 : 1;
}
