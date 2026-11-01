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
    FILE* x = fopen("task_memory_usage.wasm", "rb");
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
        return -1;
    return (int32_t)v.value.i32;
}

static OsTaskMemoryUsage usage_of(OsTaskHandle t, const char* label)
{
    OsTaskMemoryUsage u = {0};
    os_task_get_memory_usage(t, &u);
    printf("%-22s stack=%u linear=%u code=%u interpreter=%u (metadata %d) peak=%u image=%u kernel=%u total=%u\n",
           label, u.stack_bytes, u.linear_memory_bytes, u.code_bytes, u.interpreter_bytes,
           (int)u.interpreter_bytes - (int)(u.stack_bytes + u.linear_memory_bytes + u.code_bytes),
           u.interpreter_peak_bytes, u.image_bytes, u.kernel_bytes, u.total_bytes);
    if (l)
        fprintf(l, "MEASURED %s stack=%u linear=%u code=%u interpreter=%u image=%u kernel=%u total=%u\n",
                label, u.stack_bytes, u.linear_memory_bytes, u.code_bytes, u.interpreter_bytes,
                u.image_bytes, u.kernel_bytes, u.total_bytes);
    return u;
}

int main(void)
{
    Bin b = {0};
    OsTaskHandle t = NULL;
    OsTaskHandle twin = NULL;
    OsTaskMemoryUsage created;
    OsTaskMemoryUsage finished;
    OsTaskMemoryUsage other;
    OsTaskMemoryUsage before_grow;
    OsTaskMemoryUsage after_grow;
    const uint32_t stack_size = 8192U;

    l = fopen("task_memory_usage.log", "w");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load task_memory_usage.wasm");
    if (!b.d)
        return 1;

    /* A task created from host-owned bytes. */
    ok(os_task_create(&t, b.d, b.n, "app_main", "small", stack_size, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK && t,
       "create a small task with an 8 KiB stack");
    created = usage_of(t, "small, created");
    ok(created.stack_bytes >= stack_size && created.stack_bytes < stack_size + 64U, "stack is the requested size plus guard slots");
    ok(created.linear_memory_bytes >= 65536U && created.linear_memory_bytes % 65536U < 64U, "linear memory is whole 64 KiB pages plus a header");
#if d_m3HasHeapAccounting
    ok(created.interpreter_bytes >= created.stack_bytes + created.linear_memory_bytes + created.code_bytes,
       "interpreter total includes stack, memory and code");
#else
    ok(created.interpreter_bytes == 0U, "without heap accounting the interpreter total is 0");
#endif
    ok(created.image_bytes == 0U, "host-owned module bytes are not counted");
    ok(created.kernel_bytes > 0U, "task control block is counted");
#if d_m3HasHeapAccounting
    ok(created.total_bytes == created.interpreter_bytes + created.kernel_bytes, "total adds the parts");
#else
    ok(created.total_bytes == created.stack_bytes + created.linear_memory_bytes + created.code_bytes + created.kernel_bytes,
       "total adds the measured parts");
#endif
    ok(created.code_bytes > 0U, "the entry function is compiled at load");

    /* The same program costs the same in another task. */
    ok(os_task_create(&twin, b.d, b.n, "app_main", "twin", stack_size, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK && twin,
       "create the same task again");
    other = usage_of(twin, "twin, created");
    ok(other.interpreter_bytes == created.interpreter_bytes && other.code_bytes == created.code_bytes,
       "identical tasks use identical memory");
    os_task_delete(twin);

    /* A finished task releases its interpreter memory at once. */
    ok(run_to_result(t) == 1, "small task runs");
    finished = usage_of(t, "small, finished");
    ok(finished.interpreter_bytes == 0U && finished.stack_bytes == 0U && finished.linear_memory_bytes == 0U &&
       finished.code_bytes == 0U, "a dead task holds no interpreter memory");
#if d_m3HasHeapAccounting
    ok(finished.interpreter_peak_bytes >= created.interpreter_bytes, "its peak is still reported");
#endif
    ok(finished.total_bytes == finished.kernel_bytes, "only the control block remains until delete");
    os_task_delete(t);

    /* memory.grow shows up in linear memory and in the interpreter total. */
    ok(os_task_create(&t, b.d, b.n, "app_main_grow", "grow", stack_size, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK && t,
       "create a task that grows its memory");
    before_grow = usage_of(t, "grow, created");
    os_schedule();
    ok(os_task_get_state(t) == OS_TASK_WAITING, "it grew and now sleeps");
    after_grow = usage_of(t, "grow, after memory.grow");
    ok(after_grow.linear_memory_bytes == before_grow.linear_memory_bytes + 65536U, "linear memory grew by one page");
#if d_m3HasHeapAccounting
    ok(after_grow.interpreter_bytes >= before_grow.interpreter_bytes + 65536U, "the interpreter total grew with it");
#endif
    os_task_delete(t);

#if OS_HAS_PROCESSES
    /* A spawned process owns a copy of its module. */
    ok(os_task_spawn(&t, b.d, b.n, NULL, 0U, 0U, 0U) == OS_STATUS_OK && t, "spawn the module as a process");
    other = usage_of(t, "spawned, created");
    ok(other.image_bytes == b.n, "the kernel's copy of the module is counted");
    ok(other.total_bytes - other.image_bytes - other.kernel_bytes ==
           (other.interpreter_bytes ? other.interpreter_bytes : other.stack_bytes + other.linear_memory_bytes + other.code_bytes),
       "total includes the copy");
    os_task_delete(t);
#endif

    ok(os_task_get_memory_usage(NULL, &other) == OS_STATUS_INVALID_ARGUMENT, "NULL task is rejected");
    ok(os_get_task_count() == 0U, "OS is clean");
    os_shutdown();
    free(b.d);
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
