#include "os.h"
#include "os_kernel.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Several page stores (docs/decisions.md 10 of the kernel): a frame goes to
 * the store of the highest priority with a free slot, equal ones take
 * turns, priorities change while programs run; a store leaves gracefully
 * with its frames and swapped states moved, or is refused without room; a
 * lost store ends the programs on it and only them; a saved system with
 * two stores comes back.
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
    FILE* x = fopen("page_stores.wasm", "rb");
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

/* ---- Stores in host memory: each its own array of frames. */

#define FRAME 4096U
typedef struct
{
    uint8_t (*frames)[FRAME];
    uint32_t slots;
    uint32_t writes;
    uint32_t reads;
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

/* A store whose reads fail from now on, as a card that went bad. */
static Mem* g_failing;

static OsStatus mem_read(void* context, uint32_t slot, void* frame)
{
    Mem* m = (Mem*)context;
    if (slot >= m->slots || m == g_failing)
        return OS_STATUS_ERROR;
    memcpy(frame, m->frames[slot], FRAME);
    ++m->reads;
    return OS_STATUS_OK;
}

static Mem g_a, g_b, g_c, g_d;
static OsPageStore g_store_a, g_store_b, g_store_c, g_store_d;

static void make(Mem* m, OsPageStore* s, uint32_t slots, uint32_t ram_frames)
{
    free(m->frames);
    memset(m, 0, sizeof(*m));
    m->frames = calloc(slots, FRAME);
    m->slots = slots;
    memset(s, 0, sizeof(*s));
    s->context = m;
    s->slots = slots;
    s->ram_frames = ram_frames;
    s->write = mem_write;
    s->read = mem_read;
}

static uint32_t used(const OsPageStore* s)
{
    OsPageStoreStats st;
    return os_page_get_store_stats(s, &st) == OS_STATUS_OK ? st.used_slots : 0xFFFFFFFFu;
}

/* ---- A saved system in host memory. */

typedef struct
{
    uint8_t* d;
    uint32_t n;
    uint32_t cap;
    uint32_t at;
} Stream;

static OsStatus stream_write(void* context, const void* data, uint32_t size)
{
    Stream* s = (Stream*)context;
    if (s->n + size > s->cap)
    {
        uint32_t cap = (s->n + size) * 2U;
        uint8_t* d = realloc(s->d, cap);
        if (!d)
            return OS_STATUS_OUT_OF_MEMORY;
        s->d = d;
        s->cap = cap;
    }
    memcpy(s->d + s->n, data, size);
    s->n += size;
    return OS_STATUS_OK;
}

static OsStatus stream_read(void* context, void* data, uint32_t size)
{
    Stream* s = (Stream*)context;
    if (size > s->n - s->at)
        return OS_STATUS_INVALID_ARGUMENT;
    memcpy(data, s->d + s->at, size);
    s->at += size;
    return OS_STATUS_OK;
}

static Bin g_bin;

static OsStatus find(void* context, const char* name, const uint8_t** out_bytes, uint32_t* out_size)
{
    (void)context;
    (void)name;
    *out_bytes = g_bin.d;
    *out_size = g_bin.n;
    return OS_STATUS_OK;
}

/* ---- Running */

static OsTaskHandle start(const char* name)
{
    OsTaskHandle t = NULL;
    return os_task_create(&t, g_bin.d, g_bin.n, "app_main", name, 8192U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK ? t : NULL;
}

static void run(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++)
    {
        for (int s = 0; s < 8 && os_schedule() == OS_STATUS_OK; s++)
        {
        }
        os_tick(1U);
    }
}

static int dead(OsTaskHandle t) { return t == NULL || os_task_get_state(t) == OS_TASK_DEAD; }

static void run_to_end(OsTaskHandle* t, int count)
{
    for (int i = 0; i < 2000; i++)
    {
        int all = 1;
        for (int k = 0; k < count; k++)
            all &= dead(t[k]);
        if (all)
            return;
        run(1);
    }
}

static int32_t result_of(OsTaskHandle t)
{
    OsValue v;
    return os_task_get_return_value(t, &v) == OS_STATUS_OK ? v.value.i32 : -1;
}

static int32_t expected(void)
{
    static uint32_t words[49152];
    uint32_t sum = 0;
    memset(words, 0, sizeof(words));
    for (uint32_t tick = 0; tick < 12u; tick++)
        for (uint32_t i = 0; i < 49152u; i += 7u)
            words[i] += i ^ tick;
    for (uint32_t i = 0; i < 49152u; i += 7u)
        sum += words[i];
    return (int32_t)(sum & 0x7fffffff);
}

int main(void)
{
    l = fopen("page_stores.log", "w");
    ok(load(&g_bin), "load page_stores.wasm");
    if (!g_bin.d)
        return 1;
    const int32_t want = expected();

    /* 1. Priorities: B above A takes the frames; A first after a change; a full store spills. */
    os_init();
    make(&g_a, &g_store_a, 256U, 8U);
    make(&g_b, &g_store_b, 30U, 0U);
    ok(os_page_store_set(&g_store_a) == OS_STATUS_OK, "set store A, with 8 frames of RAM");
    ok(os_page_store_add(&g_store_b, 5U) == OS_STATUS_OK, "add store B at priority 5");
    {
        OsPageStore bad = g_store_b;
        bad.ram_frames = 4U;
        ok(os_page_store_add(&bad, 1U) == OS_STATUS_INVALID_ARGUMENT, "an added store cannot bring RAM");
        ok(os_page_store_add(&g_store_b, 1U) == OS_STATUS_BUSY, "nor be added twice");
    }
    {
        OsTaskHandle t = start("one");
        run(3);
        ok(used(&g_store_b) > 0U && used(&g_store_a) == 0U, "frames go to B, the higher priority");
        run(20);
        ok(used(&g_store_b) == 30U && used(&g_store_a) > 0U, "B full: frames spill to A");
        uint32_t a_writes = g_a.writes, b_writes = g_b.writes;
        ok(os_page_store_priority(&g_store_a, 9U) == OS_STATUS_OK, "A to priority 9 while it runs");
        run(5);
        ok(g_a.writes > a_writes && used(&g_store_b) <= 30U, "new frames go to A now");
        (void)b_writes;
        run_to_end(&t, 1);
        ok(dead(t) && result_of(t) == want, "the program ends with its result");
        os_task_delete(t);
        ok(used(&g_store_a) == 0U && used(&g_store_b) == 0U, "its slots are free again");
    }
    os_shutdown();

    /* 2. Equal priorities take turns. */
    os_init();
    make(&g_a, &g_store_a, 256U, 8U);
    make(&g_b, &g_store_b, 256U, 0U);
    os_page_store_set(&g_store_a);
    os_page_store_add(&g_store_b, 0U);
    {
        OsTaskHandle t = start("turns");
        run(10);
        ok(g_a.writes > 4U && g_b.writes > 4U && (g_a.writes > g_b.writes ? g_a.writes - g_b.writes : g_b.writes - g_a.writes) <= 1U,
           "A and B, both priority 0, take a frame each in turn");
        run_to_end(&t, 1);
        ok(result_of(t) == want, "and the program's memory is whole");
        os_task_delete(t);
    }
    os_shutdown();

    /* 3. Removing: refused without room, nothing moved; with room every frame and the swapped state move. */
    os_init();
    make(&g_a, &g_store_a, 40U, 8U);
    make(&g_b, &g_store_b, 256U, 0U);
    make(&g_c, &g_store_c, 20U, 0U);
    make(&g_d, &g_store_d, 256U, 0U);
    os_page_store_set(&g_store_a);
    os_page_store_add(&g_store_b, 5U);
    os_page_store_add(&g_store_c, 1U);
    {
        OsTaskHandle t[2] = { start("moved"), start("swapped") };
        run(6);
        uint32_t before_swap = used(&g_store_b);
        ok(os_task_swap_out(t[1]) == OS_STATUS_OK && t[1]->swap_image.count > 0U, "swap one program out");
        uint32_t b_used = used(&g_store_b);
        /* Its state (and frames still in RAM) went to B, the highest priority with room. */
        ok(before_swap > 20U && b_used >= before_swap + t[1]->swap_image.count, "frames and the swapped state are on B");
        ok(os_page_store_remove(&g_store_b) == OS_STATUS_BUSY && used(&g_store_b) == b_used,
           "removing B with too few slots elsewhere is refused, nothing moved");
        ok(os_page_store_add(&g_store_d, 2U) == OS_STATUS_OK, "add store D");
        ok(os_page_store_remove(&g_store_b) == OS_STATUS_OK && used(&g_store_b) == 0xFFFFFFFFu,
           "now B leaves");
        ok(used(&g_store_a) + used(&g_store_c) + used(&g_store_d) > 0U && g_d.writes > 0U,
           "its frames went to the other stores");
        run_to_end(t, 2);
        ok(dead(t[0]) && dead(t[1]) && result_of(t[0]) == want && result_of(t[1]) == want,
           "both programs end with their results: moved frames and state whole");
        os_task_delete(t[0]);
        os_task_delete(t[1]);
    }
    os_shutdown();

    /* 4. A lost store ends the programs with frames on it, and only them. */
    os_init();
    make(&g_a, &g_store_a, 256U, 8U);
    make(&g_b, &g_store_b, 256U, 0U); /* L: lost */
    make(&g_c, &g_store_c, 256U, 0U); /* K: kept */
    os_page_store_set(&g_store_a);
    os_page_store_add(&g_store_b, 9U);
    {
        OsTaskHandle on_lost = start("on_lost");
        run(4);
        ok(used(&g_store_b) > 0U, "one program pages to L");
        os_page_store_add(&g_store_c, 20U);
        OsTaskHandle on_kept = start("on_kept");
        run(4);
        ok(used(&g_store_c) > 0U, "another, started after K came, pages to K");
        ok(os_page_store_lost(&g_store_b) == OS_STATUS_OK && used(&g_store_b) == 0xFFFFFFFFu, "L is lost");
        ok(dead(on_lost) && os_task_get_exit_reason(on_lost) == OS_TASK_EXIT_WASM_ERROR &&
               os_task_get_error(on_lost) && strstr(os_task_get_error(on_lost), "page store gone"),
           "the program with frames on L ends: page store gone");
        ok(!dead(on_kept), "the other goes on");
        run_to_end(&on_kept, 1);
        ok(result_of(on_kept) == want, "and ends with its result");
        os_task_delete(on_lost);
        os_task_delete(on_kept);
        ok(used(&g_store_a) == 0U && used(&g_store_c) == 0U, "no slot is left used");
    }
    os_shutdown();

    /* 5. A saved system with two stores comes back on two fresh stores. */
    os_init();
    make(&g_a, &g_store_a, 256U, 8U);
    make(&g_b, &g_store_b, 256U, 0U);
    os_page_store_set(&g_store_a);
    os_page_store_add(&g_store_b, 5U);
    {
        OsTaskHandle t = start("saved");
        uint32_t id = os_task_get_id(t);
        Stream s = {0};
        run(6);
        ok(used(&g_store_b) > 0U && os_system_save(stream_write, &s) == OS_STATUS_OK, "save, frames on two stores");
        os_shutdown();
        os_init();
        make(&g_a, &g_store_a, 256U, 8U);
        make(&g_b, &g_store_b, 256U, 0U);
        os_page_store_set(&g_store_a);
        os_page_store_add(&g_store_b, 5U);
        ok(os_system_restore(stream_read, &s, find, NULL, 0U) == OS_STATUS_OK, "restore on fresh stores");
        t = os_task_find_by_id(id);
        run_to_end(&t, 1);
        ok(t && result_of(t) == want, "it ends with its result");
        if (t)
            os_task_delete(t);
        free(s.d);
    }
    os_shutdown();

    /* 6. A read that fails ends the program that needed the frame, and says so. */
    os_init();
    make(&g_a, &g_store_a, 256U, 8U);
    os_page_store_set(&g_store_a);
    {
        OsTaskHandle t = start("bad read");
        run(4);
        g_failing = &g_a;
        run_to_end(&t, 1);
        g_failing = NULL;
        ok(dead(t) && os_task_get_error(t) && strstr(os_task_get_error(t), "page store: a frame could not be read") &&
               !strstr(os_task_get_error(t), "exhaustion"),
           "a failed read: page store: a frame could not be read, not exhaustion");
        os_task_delete(t);
    }
    os_shutdown();

    ok(os_get_task_count() == 0U, "clean OS state");
    free(g_a.frames);
    free(g_b.frames);
    free(g_c.frames);
    free(g_d.frames);
    free(g_bin.d);
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
