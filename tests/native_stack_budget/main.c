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
    FILE* x = fopen("native_stack_budget.wasm", "rb");
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

/*
 * Built with OS_NATIVE_STACK_BUDGET=8192 and a huge slice, so without the
 * budget a 2000-deep recursion would run in one slice and take some 200 KiB
 * of native stack.
 */
static char* g_anchor;
static long g_deepest;

static m3ApiRawFunction(host_probe)
{
    char here;
    (void)runtime;
    (void)_ctx;
    (void)_sp;
    (void)_mem;
    if (g_anchor - &here > g_deepest)
        g_deepest = g_anchor - &here;
    m3ApiSuccess();
}

/* Native stack is measured from this frame, just above the scheduler. */
static __attribute__((noinline)) void schedule_once(void)
{
    char anchor;
    g_anchor = &anchor;
    os_schedule();
}

int main(void)
{
    Bin b = {0};
    OsTaskHandle t = NULL;
    OsValue arg;
    OsValue result;
    uint32_t i;

    l = fopen("native_stack_budget.log", "w");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(os_host_import_register("env", "host_probe", "v()", host_probe) == OS_STATUS_OK, "register env.host_probe");
    ok(load(&b), "load native_stack_budget.wasm");
    if (!b.d)
        return 1;

    arg.type = OS_VALUE_TYPE_I32;
    arg.value.i32 = 2000U;
    ok(os_task_create_with_args(&t, b.d, b.n, "app_main", &arg, 1U, "deep", 1024U * 1024U, OS_TASK_PRIORITY_NORMAL) ==
           OS_STATUS_OK && t,
       "create a 2000-deep recursion with a 1 MiB interpreter stack");
    for (i = 0U; t && i < 10000U && os_task_get_state(t) != OS_TASK_DEAD; i++)
        schedule_once();
    printf("deepest native stack %ld bytes over %u slices\n", g_deepest, os_task_get_run_count(t));
    if (l)
        fprintf(l, "MEASURED deepest native stack %ld bytes over %u slices\n", g_deepest, os_task_get_run_count(t));
    ok(os_task_get_exit_reason(t) == OS_TASK_EXIT_RETURNED && os_task_get_return_value(t, &result) == OS_STATUS_OK &&
           result.value.i32 == 2000U,
       "the recursion completes with the right result");
    ok(g_deepest > 0 && g_deepest < 8192 + 4096, "native stack stays near the 8 KiB budget");
    ok(os_task_get_run_count(t) > 5U, "the budget ended slices early so the native stack could unwind");
    os_task_delete(t);

    ok(os_get_task_count() == 0U, "OS is clean");
    os_shutdown();
    free(b.d);
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
