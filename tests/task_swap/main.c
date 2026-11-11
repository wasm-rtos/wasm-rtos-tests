#include "os.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    FILE* x = fopen("task_swap.wasm", "rb");
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

/* Swap storage in RAM, one image per task, with failures on request. */
/* The page store: slots in host memory, standing in for an SD card or external RAM. */
#define SLOTS 256U
static uint8_t g_slots[SLOTS][4096];
static int g_fail_write;
static int g_fail_read;

static OsStatus store_write(void* context, uint32_t slot, const void* frame)
{
    (void)context;
    if (g_fail_write || slot >= SLOTS)
        return OS_STATUS_ERROR;
    memcpy(g_slots[slot], frame, 4096U);
    return OS_STATUS_OK;
}

static OsStatus store_read(void* context, uint32_t slot, void* frame)
{
    (void)context;
    if (g_fail_read || slot >= SLOTS)
        return OS_STATUS_ERROR;
    memcpy(frame, g_slots[slot], 4096U);
    return OS_STATUS_OK;
}

static OsPageStats page_stats(void)
{
    OsPageStats stats;
    os_page_get_stats(&stats);
    return stats;
}

static OsTaskHandle create(Bin* b, const char* entry)
{
    OsTaskHandle t = NULL;
    if (os_task_create(&t, b->d, b->n, entry, entry, 0U, OS_TASK_PRIORITY_NORMAL) != OS_STATUS_OK)
        return NULL;
    return t;
}

static int32_t run_to_result(OsTaskHandle t)
{
    OsValue v;
    uint32_t i;
    for (i = 0; t && i < 200000U && os_task_get_state(t) != OS_TASK_DEAD; i++)
        os_schedule();
    if (!t || os_task_get_state(t) != OS_TASK_DEAD || os_task_get_return_value(t, &v) != OS_STATUS_OK)
        return -100000;
    return (int32_t)v.value.i32;
}

/* Run until the task blocks in its wait. */
static void run_until_waiting(OsTaskHandle t)
{
    uint32_t i;
    for (i = 0; t && i < 10000U && os_task_get_state(t) == OS_TASK_READY; i++)
        os_schedule();
}

static OsTaskMemoryUsage usage_of(OsTaskHandle t)
{
    OsTaskMemoryUsage u;
    memset(&u, 0, sizeof(u));
    os_task_get_memory_usage(t, &u);
    return u;
}

int main(void)
{
    static const OsPageStore store = { NULL, SLOTS, 64U, store_write, store_read, NULL, NULL, NULL };
    Bin b = {0};
    OsTaskHandle t = NULL;
    OsTaskMemoryUsage before;
    OsTaskMemoryUsage after;
    uint8_t byte = 0U;
    uint32_t expected = 0U;
    uint32_t swaps = 0U;

    l = fopen("task_swap.log", "w");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load task_swap.wasm");
    if (!b.d)
        return 1;

    t = create(&b, "app_main_sleeper");
    run_until_waiting(t);
    ok(os_task_swap_out(t) == OS_STATUS_UNSUPPORTED, "without a page store nothing is swapped");
    os_task_delete(t);
    ok(os_page_store_set(&store) == OS_STATUS_OK, "install a page store");

    /* A sleeping task leaves RAM and comes back when it wakes. */
    t = create(&b, "app_main_sleeper");
    run_until_waiting(t);
    ok(os_task_get_state(t) == OS_TASK_WAITING, "the sleeper filled its memory and sleeps");
    before = usage_of(t);
    ok(os_task_swap_out(t) == OS_STATUS_OK && os_task_is_swapped(t), "swap the sleeping task out");
    after = usage_of(t);
    printf("     before: %u bytes in RAM; after: %u bytes in RAM, %u of state in the store\n", before.total_bytes,
           after.total_bytes, after.swap_bytes);
    ok(after.resident_memory_bytes == 0U && after.stack_bytes == 0U && after.code_bytes == 0U,
       "its stack and code are freed, and none of its memory stays in RAM");
    ok(after.stored_memory_bytes > 0U && after.linear_memory_bytes == before.linear_memory_bytes,
       "its memory is kept, in the page store");
    ok(after.swap_bytes > 0U && after.swap_bytes < before.linear_memory_bytes / 2U, "its state is small beside the memory");
    ok(os_task_get_state(t) == OS_TASK_WAITING, "it is still waiting");
    ok(os_task_read_memory(t, 0U, &byte, 1U) == OS_STATUS_OK, "its memory can be read while swapped out");
    ok(os_task_get_snapshot_size(t, &expected) == OS_STATUS_BUSY, "but it cannot be snapshotted");
    os_tick(100U);
    ok(os_task_get_state(t) == OS_TASK_READY && os_task_is_swapped(t), "it wakes while swapped out");
    ok(run_to_result(t) == 1, "it comes back by itself and finds its memory intact");
    ok(!os_task_is_swapped(t) && usage_of(t).swap_bytes == 0U, "its state leaves the store once it is back");
    os_task_delete(t);

    /* A wait that delivers data while the task is out. */
    {
        OsQueueHandle queue = NULL;
        uint32_t item = 4242U;
        ok(os_queue_create(&queue, sizeof(item), 1U) == OS_STATUS_OK && os_queue_set_name(queue, "inbox") == OS_STATUS_OK,
           "create the host's inbox");
        t = create(&b, "app_main_receiver");
        run_until_waiting(t);
        ok(os_task_get_state(t) == OS_TASK_WAITING && os_task_swap_out(t) == OS_STATUS_OK,
           "swap out a task waiting for the queue");
        ok(os_queue_send(queue, &item) == OS_STATUS_OK && os_task_get_state(t) == OS_TASK_READY,
           "an item sent meanwhile ends its wait, straight into its memory");
        ok(os_task_swap_in(t) == OS_STATUS_OK && !os_task_is_swapped(t), "bring it back explicitly");
        ok(run_to_result(t) == 4242, "the item reached its memory and the receive returned 0");
        os_task_delete(t);
        os_queue_delete(queue);
    }

    /* Paused, swapped out, resumed: the program carries on from where it was. */
    t = create(&b, "app_main_sleeper");
    run_until_waiting(t);
    ok(os_task_suspend(t) == OS_STATUS_OK && os_task_swap_out(t) == OS_STATUS_OK,
       "pause a sleeping task and swap it out");
    ok(os_task_get_state(t) == OS_TASK_SUSPENDED && os_task_is_swapped(t), "it stays paused, out of RAM");
    ok(os_task_resume(t) == OS_STATUS_OK && run_to_result(t) == 1, "resumed, it comes back and finishes");
    os_task_delete(t);

    /* A long computation swapped out between every slice. */
    t = create(&b, "app_main_worker");
    expected = (uint32_t)run_to_result(t);
    os_task_delete(t);
    t = create(&b, "app_main_worker");
    ok(os_task_swap_out(t) == OS_STATUS_OK && usage_of(t).swap_bytes == 0U,
       "a task that has not started leaves without a state");
    while (os_task_get_state(t) != OS_TASK_DEAD && swaps < 100000U)
    {
        os_schedule();
        if (os_task_get_state(t) != OS_TASK_DEAD && os_task_swap_out(t) == OS_STATUS_OK)
            ++swaps;
    }
    printf("     swapped out %u times\n", swaps);
    ok(swaps > 3U && (uint32_t)run_to_result(t) == expected, "the result is the same as without swapping");
    os_task_delete(t);

    /* Storage failures. */
    t = create(&b, "app_main_sleeper");
    run_until_waiting(t);
    g_fail_write = 1;
    ok(os_task_swap_out(t) == OS_STATUS_ERROR && !os_task_is_swapped(t) && usage_of(t).swap_bytes == 0U,
       "a failed write leaves the task in RAM and no state behind");
    g_fail_write = 0;
    os_tick(100U);
    ok(run_to_result(t) == 1, "and it carries on");
    os_task_delete(t);

    t = create(&b, "app_main_sleeper");
    run_until_waiting(t);
    ok(os_task_swap_out(t) == OS_STATUS_OK, "swap out once more");
    g_fail_read = 1;
    ok(os_task_swap_in(t) == OS_STATUS_ERROR && os_task_is_swapped(t), "a failed read keeps it swapped out");
    os_tick(100U);
    os_schedule();
    ok(os_task_get_state(t) == OS_TASK_DEAD && os_task_get_exit_reason(t) == OS_TASK_EXIT_WASM_ERROR,
       "when the scheduler cannot bring it back, the task ends with an error");
    g_fail_read = 0;
    os_task_delete(t);
    ok(page_stats().reserved_frames == 0U && page_stats().stored_frames == 0U,
       "deleting it lets its state and memory go from the store");

    t = create(&b, "app_main_sleeper");
    run_until_waiting(t);
    ok(os_task_swap_out(t) == OS_STATUS_OK && page_stats().stored_frames > 0U, "a swapped task at shutdown");
    os_task_delete(t);
    ok(page_stats().reserved_frames == 0U && page_stats().stored_frames == 0U, "lets everything go too");

    free(b.d);
    ok(os_get_task_count() == 0U, "OS is clean");
    os_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
