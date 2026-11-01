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
typedef struct Image
{
    uint32_t id;
    uint8_t* data;
    uint32_t size;
} Image;

static Image g_images[8];
static int g_fail_write;
static int g_fail_read;
static uint32_t g_discards;

static Image* image_of(uint32_t id, int create)
{
    uint32_t i;
    for (i = 0; i < 8U; ++i)
        if (g_images[i].data && g_images[i].id == id)
            return &g_images[i];
    if (!create)
        return NULL;
    for (i = 0; i < 8U; ++i)
        if (!g_images[i].data)
        {
            g_images[i].id = id;
            g_images[i].size = 0U;
            g_images[i].data = malloc(1U);
            return g_images[i].data ? &g_images[i] : NULL;
        }
    return NULL;
}

static OsStatus swap_write(void* context, uint32_t id, uint32_t offset, const void* data, uint32_t size)
{
    Image* image = image_of(id, 1);
    uint8_t* grown;
    (void)context;
    if (g_fail_write || !image || offset != image->size)
        return OS_STATUS_ERROR;
    grown = realloc(image->data, image->size + size + 1U);
    if (!grown)
        return OS_STATUS_OUT_OF_MEMORY;
    image->data = grown;
    memcpy(image->data + image->size, data, size);
    image->size += size;
    return OS_STATUS_OK;
}

static OsStatus swap_read(void* context, uint32_t id, uint32_t offset, void* data, uint32_t size)
{
    Image* image = image_of(id, 0);
    (void)context;
    if (g_fail_read || !image || offset > image->size || size > image->size - offset)
        return OS_STATUS_ERROR;
    memcpy(data, image->data + offset, size);
    return OS_STATUS_OK;
}

static void swap_discard(void* context, uint32_t id)
{
    Image* image = image_of(id, 0);
    (void)context;
    if (image)
    {
        free(image->data);
        image->data = NULL;
        image->size = 0U;
        ++g_discards;
    }
}

static uint32_t images_held(void)
{
    uint32_t n = 0U;
    uint32_t i;
    for (i = 0; i < 8U; ++i)
        n += g_images[i].data != NULL;
    return n;
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
    static const OsSwapPort port = { NULL, swap_write, swap_read, swap_discard };
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
    ok(os_task_swap_out(t) == OS_STATUS_UNSUPPORTED, "without a port nothing is swapped");
    ok(os_swap_port_set(&port) == OS_STATUS_OK, "install a swap port");

    /* A sleeping task leaves RAM and comes back when it wakes. */
    ok(os_task_get_state(t) == OS_TASK_WAITING, "the sleeper filled its memory and sleeps");
    before = usage_of(t);
    ok(os_task_swap_out(t) == OS_STATUS_OK && os_task_is_swapped(t), "swap the sleeping task out");
    after = usage_of(t);
    printf("     before: %u bytes in RAM; after: %u bytes in RAM, %u in swap\n", before.total_bytes, after.total_bytes,
           after.swap_bytes);
    ok(after.linear_memory_bytes == 0U && after.stack_bytes == 0U && after.code_bytes == 0U,
       "its memory, stack and code are freed");
    ok(after.swap_bytes > 0U && after.swap_bytes < before.linear_memory_bytes / 2U, "the image holds less than the memory did");
    ok(os_task_get_state(t) == OS_TASK_WAITING, "it is still waiting");
    ok(os_task_read_memory(t, 0U, &byte, 1U) == OS_STATUS_BUSY, "its memory cannot be read while swapped out");
    ok(os_task_get_snapshot_size(t, &expected) == OS_STATUS_BUSY, "nor snapshotted");
    os_tick(100U);
    ok(os_task_get_state(t) == OS_TASK_READY && os_task_is_swapped(t), "it wakes while swapped out");
    ok(run_to_result(t) == 1, "it comes back by itself and finds its memory intact");
    ok(!os_task_is_swapped(t) && images_held() == 0U, "the image is discarded once it is back");
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
           "an item sent meanwhile ends its wait");
        ok(os_task_swap_in(t) == OS_STATUS_OK && !os_task_is_swapped(t), "bring it back explicitly");
        ok(run_to_result(t) == 4242, "the item reached its memory and the receive returned 0");
        os_task_delete(t);
        os_queue_delete(queue);
    }

    /* A long computation swapped out between every slice. */
    t = create(&b, "app_main_worker");
    expected = (uint32_t)run_to_result(t);
    os_task_delete(t);
    t = create(&b, "app_main_worker");
    ok(os_task_swap_out(t) == OS_STATUS_OK && usage_of(t).swap_bytes == 0U,
       "a task that has not started leaves without an image");
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
    ok(os_task_swap_out(t) == OS_STATUS_ERROR && !os_task_is_swapped(t) && images_held() == 0U,
       "a failed write leaves the task in RAM and no image behind");
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
    g_discards = 0U;
    os_task_delete(t);
    ok(g_discards == 1U && images_held() == 0U, "deleting it discards its image");

    t = create(&b, "app_main_sleeper");
    run_until_waiting(t);
    ok(os_task_swap_out(t) == OS_STATUS_OK && images_held() == 1U, "a swapped task at shutdown");
    os_task_delete(t);
    ok(images_held() == 0U, "is discarded too");

    free(b.d);
    ok(os_get_task_count() == 0U, "OS is clean");
    os_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
