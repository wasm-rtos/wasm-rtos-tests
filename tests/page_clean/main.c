#include "os.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Frames written ahead (docs/decisions.md 13 of the kernel): a task that
 * sleeps with its memory in one block, RAM three quarters full, has its
 * frames written to the store while nothing else runs; when another task
 * then needs room, the sleeper's block leaves RAM with no write, where it
 * took a write a frame with no quiet time before. The sleeper's memory
 * comes back as it was. With RAM to spare nothing is written ahead.
 */
static FILE* g_log;
static int g_failures;

static void ok(int c, const char* s)
{
    printf("%s %s\n", c ? "PASS" : "FAIL", s);
    fprintf(g_log, "%s %s\n", c ? "PASS" : "FAIL", s);
    fflush(g_log);
    g_failures += !c;
}

#define FRAME 4096U
#define SLOTS 256U
static uint8_t (*g_frames)[FRAME];
static uint32_t g_writes;

static OsStatus mem_write(void* context, uint32_t slot, const void* frame)
{
    (void)context;
    if (slot >= SLOTS)
        return OS_STATUS_ERROR;
    memcpy(g_frames[slot], frame, FRAME);
    ++g_writes;
    return OS_STATUS_OK;
}

static OsStatus mem_read(void* context, uint32_t slot, void* frame)
{
    (void)context;
    if (slot >= SLOTS)
        return OS_STATUS_ERROR;
    memcpy(frame, g_frames[slot], FRAME);
    return OS_STATUS_OK;
}

static uint8_t* g_bytes;
static uint32_t g_size;

static int load(void)
{
    FILE* file = fopen("page_clean.wasm", "rb");
    long n;
    if (!file)
        return 0;
    fseek(file, 0, SEEK_END);
    n = ftell(file);
    rewind(file);
    g_bytes = (uint8_t*)malloc((size_t)n);
    g_size = (uint32_t)fread(g_bytes, 1, (size_t)n, file);
    fclose(file);
    return g_size == (uint32_t)n;
}

static int32_t expected(void)
{
    uint32_t sum = 0;
    for (uint32_t i = 0; i < 25600u; i++)
        sum += (i * 2654435761u) ^ i;
    return (int32_t)(sum & 0x7fffffff);
}

static void run(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++)
    {
        for (int s = 0; s < 8 && os_schedule() == OS_STATUS_OK; s++)
            ;
        os_tick(1U);
    }
}

static OsTaskHandle start(const char* entry)
{
    OsTaskHandle t = NULL;
    return os_task_create(&t, g_bytes, g_size, entry, entry, 8192U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK ? t : NULL;
}

static int32_t result_of(OsTaskHandle t)
{
    OsValue v;
    v.value.i32 = -1;
    (void)os_task_get_return_value(t, &v);
    return v.value.i32;
}

/*
 * The sleeper fills its memory and sleeps; after quiet_ms of nothing to
 * do the waker comes and runs to its end. The writes from the waker's
 * creation to its end; the sleeper's result through *out_result.
 */
static uint32_t scenario(uint32_t ram_frames, uint32_t quiet_ms, uint32_t* out_ahead, int32_t* out_result)
{
    static OsPageStore store;
    OsTaskHandle sleeper, waker;
    uint32_t before, during;
    memset(&store, 0, sizeof(store));
    store.slots = SLOTS;
    store.ram_frames = ram_frames;
    store.write = mem_write;
    store.read = mem_read;
    memset(g_frames, 0, (size_t)SLOTS * FRAME);
    os_init();
    os_page_store_set(&store);
    g_writes = 0;
    sleeper = start("app_main_sleeper");
    for (int i = 0; i < 200 && os_task_get_state(sleeper) != OS_TASK_WAITING; i++)
        run(1);
    before = g_writes;
    run(quiet_ms);
    *out_ahead = g_writes - before;
    before = g_writes;
    waker = start("app_main_waker");
    for (int i = 0; i < 400 && waker && os_task_get_state(waker) != OS_TASK_DEAD; i++)
        run(1);
    during = g_writes - before;
    for (int i = 0; i < 5000 && os_task_get_state(sleeper) != OS_TASK_DEAD; i++)
        run(1);
    *out_result = result_of(sleeper);
    os_task_delete(waker);
    os_task_delete(sleeper);
    os_shutdown();
    return during;
}

int main(void)
{
    uint32_t cold, warm, spare, ahead_cold, ahead_warm, ahead_spare;
    int32_t want, got_cold, got_warm, got_spare;

    g_log = fopen("page_clean.log", "w");
    if (!g_log)
        return 1;
    ok(load(), "load page_clean.wasm");
    want = expected();
    g_frames = calloc(SLOTS, FRAME);

    /* The sleeper's 32 frames in RAM of 40 frames: 80% full. */
    warm = scenario(40U, 0U, &ahead_warm, &got_warm);
    cold = scenario(40U, 200U, &ahead_cold, &got_cold);
    spare = scenario(200U, 200U, &ahead_spare, &got_spare);
    printf("writes when the waker comes: %u right away, %u after 200 ms of quiet (%u written ahead); "
           "with RAM to spare %u ahead\n",
           warm, cold, ahead_cold, ahead_spare);
    ok(warm >= 20U, "with no quiet time, the sleeper's block is written when the waker needs room");
    ok(ahead_cold >= 20U, "with quiet time, its frames are written ahead");
    ok(cold == 0U, "and its block then leaves RAM with no write");
    ok(got_warm == want && got_cold == want, "the sleeper's memory comes back as it was, both ways");
    ok(ahead_spare == 0U && got_spare == want, "with RAM to spare, nothing is written ahead");

    free(g_frames);
    free(g_bytes);
    fprintf(g_log, "%s\n", g_failures ? "FAILED" : "ok");
    fclose(g_log);
    printf("%s\n", g_failures ? "FAILED" : "ok");
    return g_failures != 0;
}
