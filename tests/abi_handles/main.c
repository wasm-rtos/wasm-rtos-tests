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
    FILE* x = fopen("abi_handles.wasm", "rb");
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

static int32_t run_to_result(OsTaskHandle t)
{
    OsValue v;
    uint32_t i;
    for (i = 0; t && i < 50U && os_task_get_state(t) != OS_TASK_DEAD; i++)
        os_schedule();
    if (!t || os_task_get_state(t) != OS_TASK_DEAD || os_task_get_return_value(t, &v) != OS_STATUS_OK)
        return -1000;
    return (int32_t)v.value.i32;
}

static OsTaskHandle create(Bin* b, const char* entry)
{
    OsTaskHandle t = NULL;
    if (os_task_create(&t, b->d, b->n, entry, entry, 0U, OS_TASK_PRIORITY_NORMAL) != OS_STATUS_OK)
        return NULL;
    return t;
}

static void report(const char* entry, int32_t result)
{
    if (result != 0)
        printf("     %s returned %d\n", entry, (int)result);
}

int main(void)
{
    Bin b = {0};
    OsTaskHandle t = NULL;
    OsTaskHandle consumer = NULL;
    OsTaskHandle producer = NULL;
    uint32_t id;
    uint32_t alive = 0U;
    int32_t result;

    l = fopen("abi_handles.log", "w");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load abi_handles.wasm");
    if (!b.d)
        return 1;

    /* 32 handles per program; the kernel closes what a program leaves open. */
    t = create(&b, "app_main_limit");
    result = run_to_result(t);
    report("app_main_limit", result);
    ok(result == 0, "32 handles fit, the 33rd is refused, a closed slot is reused");
    for (id = 1U; id <= 33U; ++id)
        alive += os_event_group_find_by_id(id) != NULL;
    ok(alive == 0U, "every object the program left open was deleted when it ended");
    os_task_delete(t);

    /* Handles are per program. */
    t = create(&b, "app_main_foreign");
    result = run_to_result(t);
    report("app_main_foreign", result);
    ok(result == 0, "another program's handle numbers mean nothing");
    os_task_delete(t);

    /* A named queue is shared, and lives while some program holds it. */
    consumer = create(&b, "app_main_consumer");
    os_schedule();
    ok(consumer && os_task_get_state(consumer) == OS_TASK_WAITING, "consumer opens \"pipe\" and waits");
    producer = create(&b, "app_main_producer");
    result = run_to_result(producer);
    report("app_main_producer", result);
    ok(result == 0, "producer opens the same queue and sends; other limits are a mismatch");
    ok(run_to_result(consumer) == 6, "consumer received what the producer sent");
    ok(os_queue_find_by_id(1U) == NULL, "the queue went away with its last holder");
    os_task_delete(producer);
    os_task_delete(consumer);

    t = create(&b, "app_main_bad_arguments");
    result = run_to_result(t);
    report("app_main_bad_arguments", result);
    ok(result == 0, "bad names, pointers, handles and counts give their errors");
    os_task_delete(t);

#if OS_HAS_PROCESSES
    t = create(&b, "app_main_wait_any");
    os_schedule();
    ok(t && os_task_get_state(t) == OS_TASK_WAITING, "wait.any blocks on a queue and a stopped timer");
    /* The timer, the child and the final timeout need time to pass. */
    for (id = 0U; id < 20U && os_task_get_state(t) != OS_TASK_DEAD; ++id)
    {
        os_tick(10U);
        os_schedule();
    }
    result = run_to_result(t);
    report("app_main_wait_any", result);
    ok(result == 0, "wait.any reports the timer, the queue and the child, and consumes nothing");
    ok(os_get_timer_count() == 0U, "the program's timer is gone");
    os_task_delete(t);
#endif

    free(b.d);
    ok(os_get_task_count() == 0U, "OS is clean");
    os_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
