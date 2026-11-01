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
    FILE* x = fopen("task_suspend_resume.wasm", "rb");
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

static OsTaskHandle create(Bin* b, const char* entry, const char* name, OsValue* args, uint32_t argc)
{
    OsTaskHandle t = NULL;
    if (os_task_create_with_args(&t, b->d, b->n, entry, args, argc, name, 65536U, OS_TASK_PRIORITY_NORMAL) != OS_STATUS_OK)
        return NULL;
    return t;
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

int main(void)
{
    Bin b = {0};
    OsTaskHandle a = NULL;
    OsTaskHandle other = NULL;
    OsTaskHandle sleeper = NULL;
    OsTaskHandle receiver = NULL;
    OsQueueHandle queue = NULL;
    uint32_t item = 7U;
    uint32_t runs = 0U;

    l = fopen("task_suspend_resume.log", "w");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load task_suspend_resume.wasm");
    if (!b.d)
        return 1;

    /* A suspended task is never scheduled; resume makes it ready again. */
    a = create(&b, "app_main_spin", "spin_a", NULL, 0U);
    other = create(&b, "app_main_spin", "spin_b", NULL, 0U);
    ok(a && other, "create two spinning tasks");
    schedule(4U);
    ok(os_task_get_run_count(a) > 0U && os_task_get_run_count(other) > 0U, "both tasks share the processor");
    ok(os_task_suspend(a) == OS_STATUS_OK, "suspend a ready task");
    ok(os_task_get_state(a) == OS_TASK_SUSPENDED, "task is SUSPENDED");
    ok(os_task_suspend(a) == OS_STATUS_OK && os_task_get_state(a) == OS_TASK_SUSPENDED, "suspending twice is harmless");
    runs = os_task_get_run_count(a);
    schedule(6U);
    ok(os_task_get_run_count(a) == runs, "suspended task gets no slices");
    ok(os_task_get_run_count(other) > runs, "the other task keeps running");
    ok(os_task_resume(a) == OS_STATUS_OK && os_task_get_state(a) == OS_TASK_READY, "resume makes the task READY");
    schedule(4U);
    ok(os_task_get_run_count(a) > runs, "resumed task runs again");
    ok(os_task_resume(other) == OS_STATUS_OK && os_task_get_state(other) != OS_TASK_SUSPENDED,
       "resuming a task that is not suspended changes nothing");
    ok(os_task_suspend(NULL) == OS_STATUS_INVALID_ARGUMENT && os_task_resume(NULL) == OS_STATUS_INVALID_ARGUMENT,
       "NULL task is rejected");
    os_task_delete(a);
    os_task_delete(other);

    /* Suspending a delayed task cancels the delay: it continues on resume. */
    sleeper = create(&b, "app_main_sleep", "sleeper", NULL, 0U);
    ok(sleeper != NULL, "create sleeping task");
    os_schedule();
    ok(os_task_get_state(sleeper) == OS_TASK_WAITING, "task waits in its 1000 ms delay");
    os_tick(100U);
    ok(os_task_suspend(sleeper) == OS_STATUS_OK && os_task_get_state(sleeper) == OS_TASK_SUSPENDED,
       "suspend the delayed task");
    os_tick(2000U);
    ok(os_task_get_state(sleeper) == OS_TASK_SUSPENDED, "the old deadline does not wake a suspended task");
    ok(os_task_resume(sleeper) == OS_STATUS_OK, "resume the task");
    ok(run_to_result(sleeper) == 2100, "delay ended at resume, after 2100 ms");
    os_task_delete(sleeper);

    /* A suspended receiver gives up its wait: it does not take items meanwhile. */
    ok(os_queue_create(&queue, sizeof(uint32_t), 1U) == OS_STATUS_OK, "create queue");
    ok(os_queue_set_name(queue, "inbox") == OS_STATUS_OK, "name the queue");
    receiver = create(&b, "app_main_receive", "receiver", NULL, 0U);
    os_schedule();
    ok(receiver && os_task_get_state(receiver) == OS_TASK_WAITING, "receiver blocks on the empty queue");
    ok(os_task_suspend(receiver) == OS_STATUS_OK, "suspend the blocked receiver");
    ok(os_queue_send(queue, &item) == OS_STATUS_OK, "send while the receiver is suspended");
    ok(os_queue_get_count(queue) == 1U, "the suspended receiver does not take the item");
    ok(os_task_resume(receiver) == OS_STATUS_OK, "resume the receiver");
    ok(run_to_result(receiver) == -9, "its receive returns -WR_EBUSY");
    ok(os_queue_get_count(queue) == 1U, "the item is still queued");

    /* Dead tasks cannot be suspended or resumed. */
    ok(os_task_suspend(receiver) == OS_STATUS_TASK_DEAD && os_task_resume(receiver) == OS_STATUS_TASK_DEAD,
       "dead task reports TASK_DEAD");
    os_task_delete(receiver);
    os_queue_delete(queue);

    ok(os_get_task_count() == 0U, "OS is clean");
    os_shutdown();
    free(b.d);
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
