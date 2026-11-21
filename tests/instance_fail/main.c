#if defined(__APPLE__)
/* mstats() and its types beside the runner's _POSIX_C_SOURCE. */
#define _DARWIN_C_SOURCE
#endif
#include "os.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#if defined(__APPLE__)
#include <malloc/malloc.h>
#elif defined(__GLIBC__)
#include <malloc.h>
#endif

/*
 * An instantiation that fails once the imports are linked (a data segment
 * out of bounds) leaves the engine's instance to its caller: the kernel
 * frees it. The heap in use is measured around three such creations: one
 * left behind would be its memory's page, 64 KiB.
 */
static size_t heap_in_use(void)
{
#if defined(__APPLE__)
    return mstats().bytes_used;
#elif defined(__GLIBC__)
    return mallinfo2().uordblks;
#else
    return 0U;
#endif
}
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

/* One page of memory, app_main, and a byte of data at 65536: past the memory's end. */
static const uint8_t g_module[] = {
    0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x04, 0x01, 0x60, 0x00, 0x00,
    0x03, 0x02, 0x01, 0x00,
    0x05, 0x03, 0x01, 0x00, 0x01,
    0x07, 0x0C, 0x01, 0x08, 'a', 'p', 'p', '_', 'm', 'a', 'i', 'n', 0x00, 0x00,
    0x0A, 0x04, 0x01, 0x02, 0x00, 0x0B,
    0x0B, 0x09, 0x01, 0x00, 0x41, 0x80, 0x80, 0x04, 0x0B, 0x01, 0x2A,
};

int main(void)
{
    g_log = fopen("instance_fail.log", "w");
    if (!g_log)
        return 1;
    size_t before = 0U;
    expect(os_init() == OS_STATUS_OK, "initialize OS");
    /* One creation first, so what the kernel keeps once (tables, caches) is there before measuring. */
    for (int i = 0; i < 4; ++i)
    {
        if (i == 1)
            before = heap_in_use();
        OsTaskHandle task = NULL;
        OsStatus status = os_task_create(&task, g_module, (uint32_t)sizeof(g_module), "app_main", "instance_fail",
                                         4096U, OS_TASK_PRIORITY_NORMAL);
        const char* phase = os_get_last_error_phase();
        expect(status == OS_STATUS_WASM_ERROR, "a data segment out of bounds fails the task's creation");
        expect(task == NULL, "no task is left");
        expect(phase != NULL && strcmp(phase, "load_module") == 0, "it fails while instantiating");
    }
    expect(heap_in_use() < before + 16U * 1024U, "three failed creations leave no memory behind");
    os_shutdown();
    fprintf(g_log, "%s\n", g_failures ? "FAILED" : "ok");
    fclose(g_log);
    printf("%s\n", g_failures ? "FAILED" : "ok");
    return g_failures != 0;
}
