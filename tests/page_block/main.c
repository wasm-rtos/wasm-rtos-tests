#include "os.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * With a page store, a program's memory stays in one block, reached at full
 * speed, while the RAM budget has room for all of it. When another program
 * needs the RAM, the block of the one that waits leaves RAM whole: its
 * frames go to the store, and it goes on paged when it wakes, its memory as
 * it left it.
 */

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
    FILE* x = fopen("page_block.wasm", "rb");
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

/* The page store: slots in host memory, standing in for an SD card or external RAM. */
#define SLOTS 256U
static uint8_t g_slots[SLOTS][4096];

static OsStatus store_write(void* context, uint32_t slot, const void* frame)
{
    (void)context;
    if (slot >= SLOTS)
        return OS_STATUS_ERROR;
    memcpy(g_slots[slot], frame, 4096U);
    return OS_STATUS_OK;
}

static OsStatus store_read(void* context, uint32_t slot, void* frame)
{
    (void)context;
    if (slot >= SLOTS)
        return OS_STATUS_ERROR;
    memcpy(frame, g_slots[slot], 4096U);
    return OS_STATUS_OK;
}

static OsPageStats page_stats(void)
{
    OsPageStats stats;
    memset(&stats, 0, sizeof(stats));
    os_page_get_stats(&stats);
    return stats;
}

static OsTaskMemoryUsage usage_of(OsTaskHandle t)
{
    OsTaskMemoryUsage u;
    memset(&u, 0, sizeof(u));
    os_task_get_memory_usage(t, &u);
    return u;
}

static OsTaskHandle create(Bin* b, const char* entry)
{
    OsTaskHandle t = NULL;
    if (os_task_create(&t, b->d, b->n, entry, entry, 0U, OS_TASK_PRIORITY_NORMAL) != OS_STATUS_OK)
        return NULL;
    return t;
}

static int32_t result_of(OsTaskHandle t)
{
    OsValue v;
    if (!t || os_task_get_state(t) != OS_TASK_DEAD || os_task_get_return_value(t, &v) != OS_STATUS_OK)
        return -100000;
    return (int32_t)v.value.i32;
}

/* One millisecond at a time until the tasks given have ended. */
static void run_until_dead(OsTaskHandle a, OsTaskHandle b)
{
    uint32_t i;
    for (i = 0; i < 400000U && ((a && os_task_get_state(a) != OS_TASK_DEAD) || (b && os_task_get_state(b) != OS_TASK_DEAD)); i++)
    {
        os_tick(1U);
        os_schedule();
    }
}

/* Run until the task blocks in its wait. */
static void run_until_waiting(OsTaskHandle t)
{
    uint32_t i;
    for (i = 0; t && i < 10000U && os_task_get_state(t) == OS_TASK_READY; i++)
        os_schedule();
}

int main(void)
{
    /* 40 frames of RAM: one program of 2 pages (32 frames) fits in one block, not two. */
    static const OsPageStore store = { NULL, SLOTS, 40U, store_write, store_read, NULL, NULL, NULL };
    Bin b = {0};
    OsTaskHandle sleeper = NULL;
    OsTaskHandle worker = NULL;
    OsTaskMemoryUsage u;
    OsPageStats before;
    int32_t expected = 0;

    l = fopen("page_block.log", "w");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load page_block.wasm");
    if (!b.d)
        return 1;
    ok(os_page_store_set(&store) == OS_STATUS_OK, "install a page store with 40 frames of RAM");

    /* Alone, the worker's memory is one block, all of it in RAM. */
    worker = create(&b, "app_main_worker");
    u = usage_of(worker);
    if (worker && u.resident_memory_bytes == 0U)
    {
        /* An interpreter built to page every memory (WM_MEMORY_ALWAYS_PAGED) has no blocks to test. */
        printf("SKIP every memory is paged in this build\n");
        os_task_delete(worker);
        os_shutdown();
        free(b.d);
        return 0;
    }
    ok(worker && u.linear_memory_bytes >= 2U * 65536U && u.resident_memory_bytes == u.linear_memory_bytes,
       "alone, a program's memory is one block in RAM");
    run_until_dead(worker, NULL);
    expected = result_of(worker);
    ok(page_stats().stores == 0U, "and nothing went to the store");
    os_task_delete(worker);

    /* The sleeper fills its block and waits; the worker cannot have one too. */
    sleeper = create(&b, "app_main_sleeper");
    run_until_waiting(sleeper);
    u = usage_of(sleeper);
    ok(os_task_get_state(sleeper) == OS_TASK_WAITING && u.resident_memory_bytes == u.linear_memory_bytes,
       "the sleeper filled its memory, one block, and waits");
    worker = create(&b, "app_main_worker");
    u = usage_of(worker);
    ok(worker && u.resident_memory_bytes < u.linear_memory_bytes,
       "the worker's memory is paged: there is no room for a second block");

    /* The worker needs frames: the waiting sleeper's block leaves RAM whole, not the worker's own frames. */
    before = page_stats();
    {
        uint32_t i;
        for (i = 0; i < 20U; i++)
        {
            os_tick(1U);
            os_schedule();
        }
    }
    u = usage_of(sleeper);
    ok(os_task_get_state(sleeper) == OS_TASK_WAITING && u.resident_memory_bytes < u.linear_memory_bytes / 4U,
       "while it sleeps, the sleeper's memory has left RAM for the worker");
    run_until_dead(worker, sleeper);
    ok(page_stats().stores > before.stores, "the sleeper's block went to the store");
    ok(result_of(worker) == expected, "the worker's result is the same as alone");
    ok(result_of(sleeper) == 1, "the sleeper woke, paged, and found its memory as it left it");
    ok(page_stats().ram_frames <= 40U, "RAM stayed within its budget");

    os_task_delete(worker);
    os_task_delete(sleeper);
    ok(page_stats().reserved_frames == 0U && page_stats().ram_frames == 0U, "deleted: nothing reserved, nothing in RAM");
    os_shutdown();
    free(b.d);
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
