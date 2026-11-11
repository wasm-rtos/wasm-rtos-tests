#include "os.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * A page store: programs' memory is paged, at most 8 frames (32 KiB) stay
 * in RAM, and 256 slots of 4 KiB in the store (here, host memory standing
 * in for an SD card or external RAM) hold the rest.
 */
#define SLOTS 256U
#define RAM_FRAMES 8U

typedef struct
{
    uint8_t* d;
    uint32_t n;
} Bin;

static FILE* l;
static int f;
static uint8_t g_slots[SLOTS][4096];
static uint32_t g_writes;
static uint32_t g_reads;
static uint32_t g_most_in_ram;

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
    FILE* x = fopen("page_store.wasm", "rb");
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

static OsStatus store_write(void* context, uint32_t slot, const void* frame)
{
    (void)context;
    if (slot >= SLOTS)
        return OS_STATUS_INVALID_ARGUMENT;
    memcpy(g_slots[slot], frame, 4096U);
    g_writes++;
    return OS_STATUS_OK;
}

static OsStatus store_read(void* context, uint32_t slot, void* frame)
{
    (void)context;
    if (slot >= SLOTS)
        return OS_STATUS_INVALID_ARGUMENT;
    memcpy(frame, g_slots[slot], 4096U);
    g_reads++;
    return OS_STATUS_OK;
}

/* What app_main_big returns for pages. */
static uint32_t expected_sum(uint32_t pages)
{
    uint32_t count = pages * 16384U;
    uint32_t sum = 0U;
    uint32_t i;
    for (i = 0U; i < count; i += 64U)
        sum += ((i * 2654435761U) ^ (i >> 7)) ^ i;
    return sum;
}

static OsStatus create(OsTaskHandle* t, Bin* b, const char* entry, uint32_t value, uint32_t args)
{
    OsValue arg;
    arg.type = OS_VALUE_TYPE_I32;
    arg.value.i32 = (int32_t)value;
    return os_task_create_with_args(t, b->d, b->n, entry, &arg, args, entry, 16U * 1024U, OS_TASK_PRIORITY_NORMAL);
}

/* Runs until every task given is dead, noting the most frames RAM held. */
static void run(OsTaskHandle a, OsTaskHandle b)
{
    OsPageStats stats;
    uint32_t i;
    for (i = 0; i < 100000U && (os_task_get_state(a) != OS_TASK_DEAD || (b && os_task_get_state(b) != OS_TASK_DEAD)); i++)
    {
        os_schedule();
        os_page_get_stats(&stats);
        if (stats.ram_frames > g_most_in_ram)
            g_most_in_ram = stats.ram_frames;
    }
}

static uint32_t result_of(OsTaskHandle t)
{
    OsValue v;
    return os_task_get_return_value(t, &v) == OS_STATUS_OK ? (uint32_t)v.value.i32 : 0xDEADBEEFU;
}

int main(void)
{
    Bin b = {0};
    OsPageStore store = {NULL, SLOTS, RAM_FRAMES, store_write, store_read};
    OsPageStats stats;
    OsTaskMemoryUsage usage;
    OsTaskHandle t = NULL;
    OsTaskHandle u = NULL;
    OsQueueHandle queue = NULL;
    uint8_t item[16];
    l = fopen("page_store.log", "a");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load page_store.wasm");
    if (!b.d)
        return 1;
    ok(os_page_store_set(&store) == OS_STATUS_OK, "install a page store: 8 frames of RAM, 256 slots");
    os_page_get_stats(&stats);
    ok(stats.limit_frames == SLOTS && stats.reserved_frames == 0U, "it can promise a slot for every frame");

    /* One program with 256 KiB more memory than RAM holds. */
    ok(create(&t, &b, "app_main_big", 4U, 1U) == OS_STATUS_OK && t, "create a program that grows by 4 pages");
    ok(os_page_store_set(NULL) == OS_STATUS_BUSY, "the store stays while tasks exist");
    os_task_get_memory_usage(t, &usage);
    ok(usage.resident_memory_bytes < usage.linear_memory_bytes, "its memory comes into RAM only as it is touched");
    run(t, NULL);
    ok(result_of(t) == expected_sum(4U), "it reads back everything it wrote");
    ok(g_writes > 0U && g_reads > 0U, "frames went to the store and came back");
    /* Written once, then only read: a frame that came back clean leaves again without a write. */
    ok(g_writes <= usage.linear_memory_bytes / 4096U + 4U * 16U, "no frame was written to the store twice");
    ok(g_most_in_ram <= RAM_FRAMES, "RAM never held more than 8 frames");
    os_task_delete(t);
    os_page_get_stats(&stats);
    ok(stats.reserved_frames == 0U && stats.stored_frames == 0U && stats.ram_frames == 0U,
       "a deleted program leaves no frames, slots or promises");

    /* Two programs share the 8 frames, taking turns. */
    g_most_in_ram = 0U;
    ok(create(&t, &b, "app_main_big", 3U, 1U) == OS_STATUS_OK && create(&u, &b, "app_main_big", 2U, 1U) == OS_STATUS_OK,
       "create two programs");
    run(t, u);
    ok(result_of(t) == expected_sum(3U) && result_of(u) == expected_sum(2U), "both read back what they wrote");
    ok(g_most_in_ram <= RAM_FRAMES, "together they kept to 8 frames of RAM");
    os_task_delete(t);
    os_task_delete(u);

    /* A host call works on a frame that was in the store. */
    ok(os_queue_create(&queue, 16U, 1U) == OS_STATUS_OK && queue, "create a queue");
    os_queue_set_name(queue, "q");
    ok(create(&t, &b, "app_main_send", 2U, 1U) == OS_STATUS_OK, "create a program that sends from far memory");
    run(t, NULL);
    ok(result_of(t) == 0U && os_queue_receive(queue, item) == OS_STATUS_OK && memcmp(item, "from a far frame", 16U) == 0,
       "the item it sent arrives whole");
    os_task_delete(t);
    os_queue_delete(queue);

    /* Memory is promised only as far as RAM and slots go. */
    ok(os_task_create(&t, b.d, b.n, "app_main_grow_all", "grow", 16U * 1024U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK,
       "create a program that grows until it cannot");
    os_task_get_memory_usage(t, &usage);
    {
        uint32_t start_frames = usage.linear_memory_bytes / 4096U;
        run(t, NULL);
        ok(result_of(t) == (SLOTS - start_frames) / 16U, "memory.grow stops where the promise would break");
    }
    os_task_delete(t);
    os_page_get_stats(&stats);
    ok(stats.reserved_frames == 0U && stats.stored_frames == 0U, "everything is given back");
    printf("store: %u frames written, %u read\n", g_writes, g_reads);
    if (l)
        fprintf(l, "MEASURED store writes=%u reads=%u most in RAM=%u\n", g_writes, g_reads, g_most_in_ram);

    ok(os_get_task_count() == 0U, "OS is clean");
    os_shutdown();
    free(b.d);
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
