#include "os.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * A compressed page store (docs/decisions.md 12 of the kernel) above a slow
 * one: frames that compress go to it and the slow store sees few writes;
 * frames that do not (random words) go on to the slow store; the program's
 * result is right; when it ends its compressed frames are let go, so a
 * second run fits as the first did.
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
typedef struct
{
    uint8_t (*frames)[FRAME];
    uint32_t slots;
    uint32_t writes;
} Mem;

static OsStatus mem_write(void* context, uint32_t slot, const void* frame)
{
    Mem* m = (Mem*)context;
    if (slot >= m->slots)
        return OS_STATUS_ERROR;
    memcpy(m->frames[slot], frame, FRAME);
    ++m->writes;
    return OS_STATUS_OK;
}

static OsStatus mem_read(void* context, uint32_t slot, void* frame)
{
    Mem* m = (Mem*)context;
    if (slot >= m->slots)
        return OS_STATUS_ERROR;
    memcpy(frame, m->frames[slot], FRAME);
    return OS_STATUS_OK;
}

static uint8_t* g_bytes;
static uint32_t g_size;

static int load(void)
{
    FILE* file = fopen("page_compressed.wasm", "rb");
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
    static uint32_t words[49152];
    uint32_t sum = 0, seed = 1u;
    memset(words, 0, sizeof(words));
    for (uint32_t tick = 0; tick < 12u; tick++)
    {
        for (uint32_t i = 0; i < 40960u; i += 7u)
            words[i] += (i ^ tick) & 0xFFu;
        for (uint32_t i = 40960u; i < 49152u; i++)
        {
            seed = seed * 1103515245u + 12345u;
            words[i] ^= seed;
        }
    }
    for (uint32_t i = 0; i < 49152u; i++)
        sum += words[i];
    return (int32_t)(sum & 0x7fffffff);
}

/* One run of the program: its result, and how many frames the slow store wrote meanwhile. */
static int32_t run_once(Mem* slow, uint32_t* slow_writes)
{
    OsTaskHandle t = NULL;
    OsValue v;
    uint32_t before = slow->writes;
    if (os_task_create(&t, g_bytes, g_size, "app_main", "page_compressed", 8192U, OS_TASK_PRIORITY_NORMAL) != OS_STATUS_OK)
        return -1;
    for (int i = 0; i < 4000 && os_task_get_state(t) != OS_TASK_DEAD; i++)
    {
        for (int s = 0; s < 8 && os_schedule() == OS_STATUS_OK; s++)
            ;
        os_tick(1U);
    }
    *slow_writes = slow->writes - before;
    v.value.i32 = -1;
    (void)os_task_get_return_value(t, &v);
    os_task_delete(t);
    return v.value.i32;
}

int main(void)
{
    static Mem slow;
    static OsPageStore slow_store, packed_store;
    static uint32_t region[(256U * 1024U) / 4U];
    uint32_t plain_writes = 0, packed_writes = 0, again_writes = 0;
    OsPageStoreStats stats;
    int32_t want;

    g_log = fopen("page_compressed.log", "w");
    if (!g_log)
        return 1;
    ok(load(), "load page_compressed.wasm");
    want = expected();
    slow.frames = calloc(256U, FRAME);
    slow.slots = 256U;
    memset(&slow_store, 0, sizeof(slow_store));
    slow_store.context = &slow;
    slow_store.slots = 256U;
    slow_store.ram_frames = 8U;
    slow_store.write = mem_write;
    slow_store.read = mem_read;

    {
        /* Through the store itself: frames from one word repeated to random words, back byte for byte. */
        static uint8_t frame[FRAME], back[FRAME];
        uint32_t seed = 7u, fits = 0, refused = 0, wrong = 0;
        os_init();
        ok(os_page_store_compressed(&packed_store, region, sizeof(region)) == OS_STATUS_OK, "make a compressed store");
        for (uint32_t k = 0; k < 300u; k++)
        {
            uint32_t reuse = k % 10u, word = 0;
            for (uint32_t i = 0; i < FRAME / 4u; i++)
            {
                seed = seed * 1103515245u + 12345u;
                /* reuse in tenths: 9 is one word repeated, 0 every word new */
                if (i == 0 || (seed >> 24) % 10u >= reuse)
                    word = reuse == 9u ? 0xDEADBEEFu : seed ^ (seed >> (k % 7u * 4u));
                memcpy(frame + i * 4u, &word, 4u);
            }
            OsStatus status = packed_store.write(packed_store.context, k, frame);
            if (status == OS_STATUS_OK)
            {
                ++fits;
                memset(back, 0x55, sizeof(back));
                if (packed_store.read(packed_store.context, k, back) != OS_STATUS_OK || memcmp(back, frame, FRAME) != 0)
                    ++wrong;
            }
            else if (status == OS_STATUS_BUFFER_TOO_SMALL)
                ++refused;
            else
                ++wrong;
            if (k % 3u == 0u)
                packed_store.drop(packed_store.context, k);
        }
        printf("%u fit, %u refused\n", fits, refused);
        ok(wrong == 0u, "every frame that fits reads back as it was");
        ok(fits > 100u && refused > 0u, "frames that compress fit, random ones are refused");
        os_shutdown();
    }

    /* The slow store alone, to compare. */
    os_init();
    ok(os_page_store_set(&slow_store) == OS_STATUS_OK, "a slow store with 32 KB of RAM for frames");
    ok(run_once(&slow, &plain_writes) == want && plain_writes > 0U, "the program runs through the slow store alone");
    os_shutdown();

    os_init();
    ok(os_page_store_set(&slow_store) == OS_STATUS_OK, "the slow store again");
    ok(os_page_store_compressed(&packed_store, region, sizeof(region)) == OS_STATUS_OK &&
           os_page_store_add(&packed_store, 5U) == OS_STATUS_OK,
       "a compressed store of 256 KB above it, at priority 5");
    ok(os_page_store_compressed(&packed_store, region, 16U) == OS_STATUS_INVALID_ARGUMENT, "too small a region is refused");
    ok(run_once(&slow, &packed_writes) == want, "with it, the program's result is the same");
    printf("slow store writes: %u alone, %u under the compressed store\n", plain_writes, packed_writes);
    ok(packed_writes * 4U < plain_writes, "the slow store writes less than a quarter as often");
    ok(packed_writes > 0U, "frames of random words, which do not compress, still go to the slow store");
    ok(os_page_get_store_stats(&packed_store, &stats) == OS_STATUS_OK && stats.used_slots == 0U,
       "when the program ends, its compressed frames are let go");
    ok(run_once(&slow, &again_writes) == want && again_writes <= packed_writes + packed_writes / 4U + 2U,
       "a second run fits as the first did: nothing of the first is held");
    os_shutdown();

    free(slow.frames);
    free(g_bytes);
    fprintf(g_log, "%s\n", g_failures ? "FAILED" : "ok");
    fclose(g_log);
    printf("%s\n", g_failures ? "FAILED" : "ok");
    return g_failures != 0;
}
