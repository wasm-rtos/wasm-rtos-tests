#include "os.h"
#include "fs.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The whole system saved and restored into a kernel started afresh, as
 * around a deep sleep: programs that share a queue, a mutex, a semaphore,
 * an event and wait for a timer, a child process and a file end with the
 * results of a run that was never stopped, whether the system is saved once
 * or after every millisecond. The clock goes on with the time slept. A
 * stream that is cut or of another version restores nothing; a program
 * whose module changed, or whose file changed, ends alone, and the others
 * go on.
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
    FILE* x = fopen("system_snapshot.wasm", "rb");
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

/* Minimal ustar writer: appends one entry and returns the next offset. */
static uint32_t tar_add(uint8_t* image, uint32_t offset, const char* name, char type, const void* data, uint32_t size)
{
    uint8_t* h = image + offset;
    unsigned sum = 0U;
    uint32_t i;

    memset(h, 0, 512);
    strncpy((char*)h, name, 100);
    memcpy(h + 100, "0000644", 7);
    memcpy(h + 108, "0000000", 7);
    memcpy(h + 116, "0000000", 7);
    snprintf((char*)h + 124, 12, "%011o", size);
    snprintf((char*)h + 136, 12, "%011o", 0U);
    h[156] = (uint8_t)type;
    memcpy(h + 257, "ustar", 6);
    memcpy(h + 263, "00", 2);
    memset(h + 148, ' ', 8);
    for (i = 0; i < 512; i++)
        sum += h[i];
    snprintf((char*)h + 148, 8, "%06o", sum);
    h[155] = ' ';
    if (size)
        memcpy(image + offset + 512, data, size);
    return offset + 512 + (size + 511U) / 512U * 512U;
}

/* The page store: slots in host memory. */
#define SLOTS 512U
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

/* A stream in host memory, standing in for flash or an SD card. */
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

static Bin g_module;
static uint8_t* g_changed;
static const char* g_change_name;
static OsFsTar g_tar;

/* The host's modules by task name; one of them changed when the test says so. */
static OsStatus find(void* context, const char* name, const uint8_t** out_bytes, uint32_t* out_size)
{
    (void)context;
    *out_bytes = g_change_name && strcmp(name, g_change_name) == 0 ? g_changed : g_module.d;
    *out_size = g_module.n;
    return OS_STATUS_OK;
}

/* What the host does at every start: the kernel, its page store, its file system. */
static void boot(const uint8_t* tar, uint32_t tar_size)
{
    static const OsPageStore store = { NULL, SLOTS, 256U, store_write, store_read, NULL, NULL, NULL, NULL };
    os_init();
    os_page_store_set(&store);
    os_fs_tar_init(&g_tar, tar, tar_size);
    os_fs_mount("/", &os_fs_tar_driver, &g_tar);
}

#define TASKS 8
static const char* const g_names[TASKS] = {"producer", "consumer", "locker", "contender", "ticker", "parent",
                                           "reader", "spawned"};
static uint32_t g_ids[TASKS];
static uint32_t g_queue_id;

/* The host's objects and tasks, as at a first start. */
static void start(void)
{
    static const char* const entries[TASKS - 1] = {"app_main_producer", "app_main_consumer", "app_main_locker",
                                                   "app_main_contender", "app_main_ticker", "app_main_parent",
                                                   "app_main_reader"};
    OsQueueHandle queue = NULL;
    OsTimerHandle timer = NULL;
    OsTaskHandle t = NULL;
    uint32_t i;

    os_queue_create(&queue, 4U, 2U);
    os_queue_set_name(queue, "jobs");
    g_queue_id = os_queue_get_id(queue);
    for (i = 0; i < TASKS - 1; i++)
    {
        t = NULL;
        os_task_create(&t, g_module.d, g_module.n, entries[i], g_names[i], 0U, OS_TASK_PRIORITY_NORMAL);
        g_ids[i] = t ? os_task_get_id(t) : 0U;
    }
    /* A process the kernel keeps a copy of. */
    t = NULL;
    os_task_spawn(&t, g_module.d, g_module.n, NULL, 0U, 0U, 0U);
    g_ids[TASKS - 1] = t ? os_task_get_id(t) : 0U;
    /* The host's timer notifies the ticker. */
    os_timer_create(&timer, 5U, 1U, os_task_find_by_id(g_ids[4]), 1U, OS_NOTIFY_INCREMENT);
    os_timer_start(timer);
}

/* One millisecond: every task that is ready runs until none is. */
static void step(void)
{
    uint32_t guard;
    os_tick(1U);
    for (guard = 0; guard < 1000U && os_schedule() != OS_STATUS_NO_READY_TASKS; guard++)
    {
    }
}

static int all_dead(void)
{
    uint32_t i;
    for (i = 0; i < TASKS; i++)
    {
        OsTaskHandle t = os_task_find_by_id(g_ids[i]);
        if (t && os_task_get_state(t) != OS_TASK_DEAD)
            return 0;
    }
    return 1;
}

static int32_t result_of(uint32_t id)
{
    OsTaskHandle t = os_task_find_by_id(id);
    OsValue v;
    if (!t || os_task_get_state(t) != OS_TASK_DEAD || os_task_get_return_value(t, &v) != OS_STATUS_OK)
        return -100000;
    return (int32_t)v.value.i32;
}

static OsTaskState state_of(uint32_t index)
{
    OsTaskHandle t = os_task_find_by_id(g_ids[index]);
    return t ? os_task_get_state(t) : OS_TASK_DEAD;
}

static int32_t g_expected[TASKS];
static uint32_t g_expected_notifications;

static int same_results(uint32_t skip_mask)
{
    uint32_t i;
    for (i = 0; i < TASKS; i++)
    {
        if (!(skip_mask & (1U << i)) && result_of(g_ids[i]) != g_expected[i])
        {
            printf("  %s: %d, expected %d\n", g_names[i], result_of(g_ids[i]), g_expected[i]);
            return 0;
        }
    }
    return 1;
}

static uint32_t notifications(void)
{
    return os_task_get_notification_value(os_task_find_by_id(g_ids[4]));
}

/* Save, lose everything as in deep sleep, start again and restore. */
static OsStatus sleep_and_wake(Stream* s, const uint8_t* tar, uint32_t tar_size, uint32_t slept_ms, OsStatus* out_save)
{
    s->n = 0;
    s->at = 0;
    *out_save = os_system_save(stream_write, s);
    if (*out_save != OS_STATUS_OK)
        return *out_save;
    os_shutdown();
    boot(tar, tar_size);
    return os_system_restore(stream_read, s, find, NULL, slept_ms);
}

int main(void)
{
    static uint8_t tar[64 * 1024];
    static uint8_t changed_tar[8 * 1024];
    static const char data[] = "Every byte of this file is read five at a time, with a sleep between reads.\n";
    static const char changed_data[] = "Shorter now.\n";
    uint32_t tar_size = 0;
    uint32_t changed_size = 0;
    Stream s = {0};
    OsStatus saved = OS_STATUS_OK;
    uint32_t i;

    l = fopen("system_snapshot.log", "w");
    ok(load(&g_module), "load system_snapshot.wasm");
    if (!g_module.d)
        return 1;
    tar_size = tar_add(tar, tar_size, "bin/system_snapshot.wasm", '0', g_module.d, g_module.n);
    tar_size = tar_add(tar, tar_size, "data.txt", '0', data, sizeof(data) - 1U);
    memset(tar + tar_size, 0, 1024);
    tar_size += 1024;
    /* After the sleep: the program is gone from the card, the file is another. */
    changed_size = tar_add(changed_tar, changed_size, "data.txt", '0', changed_data, sizeof(changed_data) - 1U);
    memset(changed_tar + changed_size, 0, 1024);
    changed_size += 1024;
    g_changed = malloc(g_module.n);
    memcpy(g_changed, g_module.d, g_module.n);
    g_changed[g_module.n - 1U] ^= 0x01U;

    /* The run that is never stopped. */
    boot(tar, tar_size);
    start();
    for (i = 0; i < 2000U && !all_dead(); i++)
        step();
    for (i = 0; i < TASKS; i++)
        g_expected[i] = result_of(g_ids[i]);
    g_expected_notifications = notifications();
    {
        int every = 1;
        for (i = 0; i < TASKS; i++)
            every &= g_expected[i] != -100000;
        ok(every, "uninterrupted: every program ends");
    }
    os_shutdown();

    /* Saved once, mid-way, with one program swapped out of RAM. */
    boot(tar, tar_size);
    start();
    for (i = 0; i < 10U; i++)
        step();
    ok(state_of(2) == OS_TASK_WAITING && state_of(3) == OS_TASK_WAITING && state_of(5) == OS_TASK_WAITING &&
           state_of(6) == OS_TASK_WAITING,
       "at the save the locker sleeps holding the mutex, the contender, the parent and the reader wait");
    ok(os_task_swap_out(os_task_find_by_id(g_ids[1])) == OS_STATUS_OK, "the consumer is swapped out of RAM");
    ok(sleep_and_wake(&s, tar, tar_size, 0U, &saved) == OS_STATUS_OK && saved == OS_STATUS_OK,
       "the system is saved, the kernel shut down, started again and restored");
    printf("  stream: %u bytes\n", s.n);
    ok(os_get_task_count() > 0U && os_queue_find_by_id(g_queue_id) != NULL &&
           os_queue_get_item_size(os_queue_find_by_id(g_queue_id)) == 4U,
       "the host finds its queue by the id it had");
    {
        OsQueueHandle extra = NULL;
        ok(os_queue_create(&extra, 4U, 1U) == OS_STATUS_OK && os_queue_get_id(extra) != g_queue_id &&
               os_queue_find_by_id(g_queue_id) != extra,
           "an object made after the restore gets an id of its own");
        os_queue_delete(extra);
    }
    for (i = 0; i < 2000U && !all_dead(); i++)
        step();
    ok(same_results(0U), "every program ends with the result of the run never stopped");
    ok(notifications() == g_expected_notifications, "the host's timer notified the ticker as often");
    os_shutdown();

    /* Saved and restored after every millisecond. */
    {
        uint32_t restores = 0;
        int all_ok = 1;
        boot(tar, tar_size);
        start();
        for (i = 0; i < 2000U && !all_dead(); i++)
        {
            step();
            if (sleep_and_wake(&s, tar, tar_size, 0U, &saved) != OS_STATUS_OK)
            {
                all_ok = 0;
                break;
            }
            ++restores;
        }
        printf("  %u restores\n", restores);
        ok(all_ok && restores > 50U, "saved and restored after every millisecond");
        ok(same_results(0U) && notifications() == g_expected_notifications,
           "and every program still ends with the same result");
        os_shutdown();
    }

    /* The clock goes on with the time slept. */
    {
        OsTaskHandle sleeper = NULL;
        uint32_t ticks = 0;
        int32_t took;
        boot(tar, tar_size);
        os_task_create(&sleeper, g_module.d, g_module.n, "app_main_sleeper", "sleeper", 0U, OS_TASK_PRIORITY_NORMAL);
        g_ids[0] = os_task_get_id(sleeper);
        for (i = 0; i < 30U; i++)
            step();
        ok(sleep_and_wake(&s, tar, tar_size, 50U, &saved) == OS_STATUS_OK, "a sleeper saved 30 ms into 100 ms, asleep 50 ms");
        for (ticks = 0; ticks < 200U && os_task_get_state(os_task_find_by_id(g_ids[0])) != OS_TASK_DEAD; ticks++)
            step();
        took = result_of(g_ids[0]);
        printf("  woke %u ms after the restore, its sleep took %d ms\n", ticks, took);
        ok(ticks >= 19U && ticks <= 21U && took >= 100 && took <= 101,
           "it wakes 20 ms after the restore and sees its 100 ms go by");
        os_shutdown();
    }

    /* A stream taken mid-way, for the cases below. */
    boot(tar, tar_size);
    start();
    for (i = 0; i < 10U; i++)
        step();
    s.n = 0;
    ok(os_system_save(stream_write, &s) == OS_STATUS_OK, "save mid-way again");
    os_shutdown();

    /* Cut or of another version: nothing is restored, and the kernel works. */
    {
        Stream cut = s;
        Stream other = s;
        OsTaskHandle t = NULL;
        cut.n = s.n / 2U;
        cut.at = 0;
        boot(tar, tar_size);
        ok(os_system_restore(stream_read, &cut, find, NULL, 0U) != OS_STATUS_OK && os_get_task_count() == 0U &&
               os_queue_find_by_id(g_queue_id) == NULL,
           "a cut stream restores nothing");
        ok(os_task_create(&t, g_module.d, g_module.n, "app_main", "after", 0U, OS_TASK_PRIORITY_NORMAL) ==
               OS_STATUS_OK && os_task_get_id(t) == 1U,
           "and the kernel starts tasks as if new");
        for (i = 0; i < 100U && os_task_get_state(t) != OS_TASK_DEAD; i++)
            step();
        ok(os_task_get_state(t) == OS_TASK_DEAD && os_task_get_exit_reason(t) == OS_TASK_EXIT_RETURNED,
           "which run to the end");
        os_shutdown();

        other.d = malloc(s.n);
        memcpy(other.d, s.d, s.n);
        other.d[4] ^= 0x7FU;
        other.at = 0;
        boot(tar, tar_size);
        ok(os_system_restore(stream_read, &other, find, NULL, 0U) != OS_STATUS_OK && os_get_task_count() == 0U,
           "a stream of another version restores nothing");
        os_shutdown();
        free(other.d);
    }

    /* The world changed in the sleep: one module, the file, the child's program. */
    {
        OsTaskHandle locker;
        OsTaskHandle reader;
        const char* error;
        s.at = 0;
        boot(changed_tar, changed_size);
        g_change_name = "locker";
        ok(os_system_restore(stream_read, &s, find, NULL, 0U) == OS_STATUS_OK, "restore after the world changed");
        g_change_name = NULL;
        locker = os_task_find_by_id(g_ids[2]);
        reader = os_task_find_by_id(g_ids[6]);
        error = os_task_get_error(locker);
        ok(os_task_get_state(locker) == OS_TASK_DEAD && os_task_get_exit_reason(locker) == OS_TASK_EXIT_WASM_ERROR &&
               error && strcmp(error, "restore: its module changed") == 0,
           "the locker, whose module changed, ended: restore: its module changed");
        error = os_task_get_error(reader);
        ok(os_task_get_state(reader) == OS_TASK_DEAD && error && strcmp(error, "restore: a file it had open changed") == 0,
           "the reader, whose file changed, ended: restore: a file it had open changed");
        for (i = 0; i < 2000U && !all_dead(); i++)
            step();
        ok(state_of(3) == OS_TASK_DEAD && os_task_get_exit_reason(os_task_find_by_id(g_ids[3])) == OS_TASK_EXIT_RETURNED,
           "the contender got the mutex the locker held and ended");
        ok(state_of(5) == OS_TASK_DEAD && os_task_get_exit_reason(os_task_find_by_id(g_ids[5])) == OS_TASK_EXIT_RETURNED &&
               result_of(g_ids[5]) != g_expected[5],
           "the parent, whose child's program is gone, got its child's end and ended");
        ok(same_results((1U << 2) | (1U << 3) | (1U << 5) | (1U << 6)),
           "every other program ends with the result of the run never stopped");
        os_shutdown();
    }

    free(s.d);
    free(g_changed);
    free(g_module.d);
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
