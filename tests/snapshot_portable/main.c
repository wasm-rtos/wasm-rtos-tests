#include "os.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct
{
    uint8_t* d;
    uint32_t n;
} Bin;

static FILE* l;
static int f;

static void ok(int c, const char* s)
{
    printf("%s %s\n", c ? "PASS" : "FAIL", s);
    if (l)
    {
        fprintf(l, "%s %s\n", c ? "PASS" : "FAIL", s);
        fflush(l);
    }
    if (!c)
        f++;
}

static int load(Bin* b)
{
    FILE* x = fopen("snapshot_portable.wasm", "rb");
    long n;
    if (!x)
        return 0;
    fseek(x, 0, SEEK_END);
    n = ftell(x);
    rewind(x);
    if (n <= 0)
        return fclose(x), 0;
    b->d = malloc((size_t)n);
    b->n = (uint32_t)n;
    if (!b->d || fread(b->d, 1, (size_t)n, x) != (size_t)n)
    {
        fclose(x);
        free(b->d);
        b->d = NULL;
        return 0;
    }
    fclose(x);
    return 1;
}

/* (module (func (export "app_main") (result i32) i32.const 42)) */
static const uint8_t other_module[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x05, 0x01, 0x60, 0x00, 0x01, 0x7f,
    0x03, 0x02, 0x01, 0x00,
    0x07, 0x0c, 0x01, 0x08, 'a', 'p', 'p', '_', 'm', 'a', 'i', 'n', 0x00, 0x00,
    0x0a, 0x06, 0x01, 0x04, 0x00, 0x41, 0x2a, 0x0b
};

static OsTaskHandle create(Bin* b)
{
    OsTaskHandle t = NULL;
    if (os_task_create(&t, b->d, b->n, "app_main", "portable", 0U, OS_TASK_PRIORITY_NORMAL) != OS_STATUS_OK)
        return NULL;
    return t;
}

static uint32_t result_of(OsTaskHandle t)
{
    OsValue v;
    if (!t || os_task_get_state(t) != OS_TASK_DEAD || os_task_get_return_value(t, &v) != OS_STATUS_OK)
        return 0xdeadU;
    return (uint32_t)v.value.i32;
}

int main(void)
{
    Bin b = {0};
    OsTaskHandle t = NULL;
    uint32_t expected = 0U;
    uint32_t hops = 0U;
    uint32_t largest = 0U;
    uint32_t memory_bytes = 0U;
    uint8_t* image = NULL;
    uint32_t size = 0U;
    int ok_all = 1;

    l = fopen("snapshot_portable.log", "w");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load snapshot_portable.wasm");
    if (!b.d)
        return 1;

    /* The answer without interruptions. */
    t = create(&b);
    while (t && os_task_get_state(t) != OS_TASK_DEAD)
        os_schedule();
    expected = result_of(t);
    ok(t && os_task_get_exit_reason(t) == OS_TASK_EXIT_RETURNED, "reference run finishes");
    os_task_delete(t);

    /*
     * After every slice the program moves to a new task: its code is compiled
     * afresh, in another order and onto other code pages, so only positions
     * named by the snapshot itself can work.
     */
    t = create(&b);
    while (t && ok_all)
    {
        OsTaskHandle next = NULL;
        OsTaskMemoryUsage usage;

        os_schedule();
        if (os_task_get_state(t) == OS_TASK_DEAD)
            break;
        ok_all = os_task_get_snapshot_size(t, &size) == OS_STATUS_OK && size > 0U;
        if (!ok_all)
            break;
        free(image);
        image = malloc(size);
        ok_all = image && os_task_save_snapshot(t, image, size, &size) == OS_STATUS_OK;
        next = ok_all ? create(&b) : NULL;
        ok_all = ok_all && next && os_task_load_snapshot(next, image, size) == OS_STATUS_OK;
        if (os_task_get_memory_usage(t, &usage) == OS_STATUS_OK)
            memory_bytes = usage.linear_memory_bytes;
        if (size > largest)
            largest = size;
        os_task_delete(t);
        t = next;
        ++hops;
    }
    ok(ok_all, "every slice was saved and loaded into a new task");
    printf("     %u hops, largest snapshot %u bytes, linear memory %u bytes\n", hops, largest, memory_bytes);
    ok(hops > 20U, "the program moved many times, stopped at many different places");
    ok(t && result_of(t) == expected, "the moved program computes what the uninterrupted one did");
    ok(largest > 0U && largest < memory_bytes / 4U, "zero runs keep snapshots far smaller than memory");
    os_task_delete(t);

    /* Snapshots only load where they belong. */
    t = create(&b);
    os_schedule();
    size = 0U;
    ok(os_task_get_snapshot_size(t, &size) == OS_STATUS_OK, "take one more snapshot");
    free(image);
    image = malloc(size);
    ok(image && os_task_save_snapshot(t, image, size, &size) == OS_STATUS_OK, "save it");
    {
        OsTaskHandle other = NULL;
        OsTaskHandle fresh = NULL;

        ok(os_task_create(&other, (uint8_t*)other_module, sizeof(other_module), "app_main", "other", 0U,
                          OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK &&
               os_task_load_snapshot(other, image, size) != OS_STATUS_OK,
           "another program refuses the snapshot");
        while (other && os_task_get_state(other) != OS_TASK_DEAD)
            os_schedule();
        ok(result_of(other) == 42U, "and still runs as it was created");
        os_task_delete(other);

        /* Nothing is scheduled until both loads are done. */
        fresh = create(&b);
        ok(os_task_load_snapshot(fresh, image, size / 2U) != OS_STATUS_OK, "a truncated snapshot is refused");
        ok(os_task_load_snapshot(fresh, image, size) == OS_STATUS_OK, "the same task then loads the whole one");
        ok(os_task_load_snapshot(fresh, image, size) == OS_STATUS_BUSY, "a task that has state refuses another load");
        while (fresh && os_task_get_state(fresh) != OS_TASK_DEAD)
            os_schedule();
        ok(result_of(fresh) == expected, "it finishes with the right answer");
        os_task_delete(fresh);
    }
    os_task_delete(t);

    free(image);
    free(b.d);
    ok(os_get_task_count() == 0U, "OS is clean");
    os_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
