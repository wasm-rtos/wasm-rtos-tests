#include "os.h"
#include "vm/source/wm.h"
#include "vm/source/wm_heap.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * The kernel over the interpreter's own heap (vm decision 20): with
 * wm_heap_use before os_init, predecoded records come from that heap, and
 * when the task is gone and the code cache trimmed, the heap is whole again.
 * With the kernel's allocations from the same heap (as a port gives it),
 * everything comes back in one piece.
 */

typedef struct
{
    uint8_t* data;
    uint32_t size;
} Binary;

static FILE* log_file;
static int failures;
static uint8_t g_arena[256U * 1024U];

static void expect(int condition, const char* text)
{
    printf("%s %s\n", condition ? "PASS" : "FAIL", text);
    if (log_file)
    {
        fprintf(log_file, "%s %s\n", condition ? "PASS" : "FAIL", text);
        fflush(log_file);
    }
    if (!condition)
        ++failures;
}

static int load(Binary* binary)
{
    FILE* file = fopen("vm_heap.wasm", "rb");
    long size;
    if (!file)
        return 0;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    rewind(file);
    binary->data = (uint8_t*)malloc((size_t)size);
    binary->size = (uint32_t)size;
    if (!binary->data || fread(binary->data, 1, (size_t)size, file) != (size_t)size)
    {
        fclose(file);
        free(binary->data);
        return 0;
    }
    fclose(file);
    return 1;
}

int main(void)
{
    Binary binary = {0};
    OsTaskHandle task = NULL;
    WmHeap* heap = wm_heap_new(g_arena, sizeof(g_arena));
    WmHeapStats before, during = {0}, after, now;
    uint32_t i;
    size_t code_during = 0U;

    log_file = fopen("vm_heap.log", "w");
    expect(heap != NULL, "make the interpreter's heap");
    wm_heap_use(heap);
    wm_heap_stats(heap, &before);
    expect(os_init() == OS_STATUS_OK, "initialize OS over the heap");
    expect(load(&binary), "load vm_heap.wasm");
    if (binary.data)
    {
        expect(os_task_create(&task, binary.data, binary.size, "app_main", "vm_heap", 64U * 1024U,
                              OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK &&
                   task != NULL,
               "create task");
        for (i = 0; task && i < 100000U && os_task_get_state(task) != OS_TASK_DEAD; ++i)
        {
            if (os_schedule() != OS_STATUS_OK)
                break;
            /* The most records the cache held, and the heap then. */
            if (wm_code_cache_bytes() > code_during)
            {
                code_during = wm_code_cache_bytes();
                wm_heap_stats(heap, &now);
                during = now;
            }
        }
        if (task)
        {
            printf("slices %u, records %zu, heap used %zu -> %zu\n", (unsigned)i, code_during, before.used, during.used);
            expect(os_task_get_state(task) == OS_TASK_DEAD, "task ends");
            expect(os_task_get_exit_code(task) == 0U, "task's result right");
            /* Sanitizer builds run in place (wm_module_predecoded): no records then. */
            expect(during.used >= before.used + code_during, "records come from the interpreter's heap");
            expect(wm_heap_check(heap), "heap whole while code is cached");
            expect(os_task_delete(task) == OS_STATUS_OK, "delete task");
        }
        free(binary.data);
    }
    expect(os_get_task_count() == 0U, "clean OS state");
    os_shutdown();
    wm_code_cache_trim(0U);
    wm_heap_stats(heap, &after);
    expect(wm_code_cache_bytes() == 0U, "code cache empty after trim");
    expect(wm_heap_check(heap), "heap checks after shutdown");
    expect(after.used == before.used, "everything back to the heap");
    expect(after.largest == after.free, "free memory in one piece");
    wm_heap_use(NULL);
    if (log_file)
        fclose(log_file);
    return failures ? 1 : 0;
}
