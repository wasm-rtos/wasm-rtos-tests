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
    FILE* x = fopen("task_set_priority.wasm", "rb");
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

static void schedule(uint32_t slices)
{
    uint32_t i;
    for (i = 0; i < slices; i++)
        os_schedule();
}

static int32_t run_to_result(OsTaskHandle t)
{
    OsValue v;
    uint32_t i;
    for (i = 0; t && i < 50U && os_task_get_state(t) != OS_TASK_DEAD; i++)
        os_schedule();
    if (!t || os_task_get_state(t) != OS_TASK_DEAD || os_task_get_return_value(t, &v) != OS_STATUS_OK)
        return -1;
    return (int32_t)v.value.i32;
}

static OsTaskHandle create_with_priority(Bin* b, const char* entry, const char* name, uint32_t priority)
{
    OsTaskHandle t = NULL;
    if (os_task_create(&t, b->d, b->n, entry, name, 65536U, priority) != OS_STATUS_OK)
        return NULL;
    return t;
}

int main(void)
{
    Bin b = {0};
    OsTaskHandle low = NULL;
    OsTaskHandle high = NULL;
    OsTaskHandle done = NULL;
    uint32_t low_runs = 0U;
    uint32_t high_runs = 0U;

    l = fopen("task_set_priority.log", "w");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load task_set_priority.wasm");
    if (!b.d)
        return 1;

    /* The scheduler always runs the highest priority ready task. */
    low = create_with_priority(&b, "app_main_spin", "low", OS_TASK_PRIORITY_NORMAL);
    high = create_with_priority(&b, "app_main_spin", "high", OS_TASK_PRIORITY_HIGH);
    ok(low && high, "create a normal and a high priority task");
    schedule(4U);
    ok(os_task_get_run_count(low) == 0U && os_task_get_run_count(high) == 4U, "only the high priority task runs");

    ok(os_task_set_priority(low, OS_TASK_PRIORITY_REALTIME) == OS_STATUS_OK, "raise the other task above it");
    ok(os_task_get_priority(low) == OS_TASK_PRIORITY_REALTIME && os_task_get_base_priority(low) == OS_TASK_PRIORITY_REALTIME,
       "new priority is reported");
    high_runs = os_task_get_run_count(high);
    schedule(4U);
    ok(os_task_get_run_count(low) == 4U && os_task_get_run_count(high) == high_runs, "now only the raised task runs");

    ok(os_task_set_priority(high, OS_TASK_PRIORITY_REALTIME) == OS_STATUS_OK, "give both the same priority");
    low_runs = os_task_get_run_count(low);
    high_runs = os_task_get_run_count(high);
    schedule(6U);
    ok(os_task_get_run_count(low) - low_runs == 3U && os_task_get_run_count(high) - high_runs == 3U,
       "equal priorities take turns");

    ok(os_task_set_priority(low, OS_TASK_PRIORITY_IDLE) == OS_STATUS_OK && os_task_get_priority(low) == OS_TASK_PRIORITY_IDLE,
       "lower a task to idle");
    low_runs = os_task_get_run_count(low);
    schedule(3U);
    ok(os_task_get_run_count(low) == low_runs, "the idle task waits while another task is ready");

    done = create_with_priority(&b, "app_main_done", "done", OS_TASK_PRIORITY_NORMAL);
    ok(done && os_task_set_priority(done, OS_TASK_PRIORITY_HIGH) == OS_STATUS_OK, "change the priority of a task that has not run");
    os_task_suspend(high);
    run_to_result(done);
    ok(os_task_get_state(done) == OS_TASK_DEAD, "it runs and finishes");
    ok(os_task_set_priority(done, OS_TASK_PRIORITY_LOW) == OS_STATUS_TASK_DEAD, "dead task reports TASK_DEAD");
    ok(os_task_set_priority(NULL, 1U) == OS_STATUS_INVALID_ARGUMENT, "NULL task is rejected");

    os_task_delete(low);
    os_task_delete(high);
    os_task_delete(done);
    ok(os_get_task_count() == 0U, "OS is clean");
    os_shutdown();
    free(b.d);
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
