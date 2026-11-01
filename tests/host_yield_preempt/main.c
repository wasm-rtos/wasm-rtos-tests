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
    FILE* x = fopen("host_yield_preempt.wasm", "rb");
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

static uint32_t g_counter_address;

static m3ApiRawFunction(host_yield)
{
    (void)runtime;
    (void)_ctx;
    (void)_sp;
    (void)_mem;
    os_task_yield();
    m3ApiSuccess();
}

static m3ApiRawFunction(host_preempt)
{
    (void)runtime;
    (void)_ctx;
    (void)_sp;
    (void)_mem;
    os_request_preempt();
    m3ApiSuccess();
}

static m3ApiRawFunction(host_publish)
{
    m3ApiGetArg(uint32_t, address);
    (void)runtime;
    (void)_ctx;
    (void)_mem;
    g_counter_address = address;
    m3ApiSuccess();
}

/* Returns the number of slices until the task died, or 0 if it never did. */
static uint32_t slices_until_dead(OsTaskHandle t)
{
    uint32_t i;
    for (i = 1U; i <= 20U; i++)
    {
        os_schedule();
        if (os_task_get_state(t) == OS_TASK_DEAD)
            return i;
    }
    return 0U;
}

static uint32_t read_counter(OsTaskHandle t)
{
    uint32_t value = 0U;
    os_task_read_memory(t, g_counter_address, (uint8_t*)&value, sizeof(value));
    return value;
}

int main(void)
{
    Bin b = {0};
    OsTaskHandle t = NULL;
    OsValue arg;
    OsValue result;
    uint32_t before = 0U;
    uint32_t full_slice = 0U;
    uint32_t preempted_slice = 0U;
    uint32_t next_slice = 0U;

    l = fopen("host_yield_preempt.log", "w");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(os_host_import_register("env", "host_yield", "v()", host_yield) == OS_STATUS_OK &&
       os_host_import_register("env", "host_preempt", "v()", host_preempt) == OS_STATUS_OK &&
       os_host_import_register("env", "host_publish", "v(i)", host_publish) == OS_STATUS_OK,
       "register host imports");
    ok(load(&b), "load host_yield_preempt.wasm");
    if (!b.d)
        return 1;

    ok(os_task_yield() == OS_STATUS_TASK_NOT_FOUND, "yield outside a task is rejected");

    /* os_task_yield inside a host import ends the caller's slice. */
    arg.type = OS_VALUE_TYPE_I32;
    arg.value.i32 = 3U;
    t = create(&b, "app_main_yield", "yielder", &arg, 1U);
    ok(t != NULL, "create task that yields 3 times");
    ok(slices_until_dead(t) == 4U, "each yield ends a slice: 3 yields + 1 return = 4 slices");
    ok(os_task_get_return_value(t, &result) == OS_STATUS_OK && result.value.i32 == 3U, "task finished all rounds");
    os_task_delete(t);

    /* os_request_preempt inside a host import does the same. */
    t = create(&b, "app_main_preempt", "preempted", &arg, 1U);
    ok(t != NULL, "create task that requests preemption 3 times");
    ok(slices_until_dead(t) == 4U, "each request ends a slice: 4 slices");
    ok(os_task_get_return_value(t, &result) == OS_STATUS_OK && result.value.i32 == 3U, "task finished all rounds");
    os_task_delete(t);

    /* Requested between slices (as an interrupt would), it cuts the next slice short once. */
    t = create(&b, "app_main_count", "counter", NULL, 0U);
    ok(t != NULL, "create counting task");
    os_schedule();
    ok(g_counter_address != 0U, "task published its counter");
    before = read_counter(t);
    os_schedule();
    full_slice = read_counter(t) - before;
    os_request_preempt();
    before = read_counter(t);
    os_schedule();
    preempted_slice = read_counter(t) - before;
    before = read_counter(t);
    os_schedule();
    next_slice = read_counter(t) - before;
    printf("counts per slice: full=%u preempted=%u next=%u\n", full_slice, preempted_slice, next_slice);
    ok(full_slice > 0U, "a full slice makes progress");
    ok(preempted_slice < full_slice / 2U, "a pending preemption cuts the next slice short");
    ok(next_slice == full_slice, "the request is used up: the following slice is full again");
    os_task_delete(t);

    ok(os_get_task_count() == 0U, "OS is clean");
    os_shutdown();
    free(b.d);
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
