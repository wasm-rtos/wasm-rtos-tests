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
    FILE* x = fopen("clock_update_time.wasm", "rb");
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

static OsTaskHandle sleep_task(Bin* b, uint32_t delay_ms)
{
    OsValue arg;
    arg.type = OS_VALUE_TYPE_I32;
    arg.value.i32 = delay_ms;
    return create(b, "app_main_sleep", "sleeper", &arg, 1U);
}

int main(void)
{
    Bin b = {0};
    OsTaskHandle t = NULL;

    l = fopen("clock_update_time.log", "w");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load clock_update_time.wasm");
    if (!b.d)
        return 1;

    /* os_update_time_ms takes an absolute clock and advances by the difference. */
    ok(os_get_tick_ms() == 0U, "kernel time starts at 0");
    ok(os_update_time_ms(5000U) == OS_STATUS_OK && os_get_tick_ms() == 0U, "the first update only sets the baseline");
    t = sleep_task(&b, 100U);
    os_schedule();
    ok(t && os_task_get_state(t) == OS_TASK_WAITING, "task sleeps 100 ms");
    ok(os_update_time_ms(5060U) == OS_STATUS_OK && os_get_tick_ms() == 60U, "advance by the 60 ms difference");
    ok(os_task_get_state(t) == OS_TASK_WAITING, "task still sleeps before its deadline");
    ok(os_update_time_ms(5060U) == OS_STATUS_OK && os_get_tick_ms() == 60U, "the same timestamp does not advance time");
    ok(os_update_time_ms(5100U) == OS_STATUS_OK && os_get_tick_ms() == 100U, "reach the deadline");
    ok(os_task_get_state(t) == OS_TASK_READY, "task wakes at its deadline");
    ok(run_to_result(t) == 100, "task slept exactly 100 ms");
    os_task_delete(t);

    /* os_tick advances relative time and drops the baseline. */
    os_tick(5U);
    ok(os_get_tick_ms() == 105U, "os_tick advances time");
    ok(os_update_time_ms(9000U) == OS_STATUS_OK && os_get_tick_ms() == 105U, "after os_tick the next update sets a new baseline");
    ok(os_update_time_ms(9010U) == OS_STATUS_OK && os_get_tick_ms() == 115U, "then updates advance again");

    /* The clock may wrap around 32 bits. */
    os_tick(0U);
    ok(os_update_time_ms(0xFFFFFFF0U) == OS_STATUS_OK && os_get_tick_ms() == 115U, "baseline just before the clock wraps");
    t = sleep_task(&b, 50U);
    os_schedule();
    ok(t && os_task_get_state(t) == OS_TASK_WAITING, "task sleeps 50 ms");
    ok(os_update_time_ms(0x10U) == OS_STATUS_OK && os_get_tick_ms() == 147U, "a wrapped clock advances by 32 ms");
    ok(os_task_get_state(t) == OS_TASK_WAITING, "task still sleeps");
    ok(os_update_time_ms(0x22U) == OS_STATUS_OK && os_task_get_state(t) == OS_TASK_READY, "task wakes 50 ms after it slept");
    ok(run_to_result(t) == 50, "task slept exactly 50 ms across the wrap");
    os_task_delete(t);

    ok(os_get_task_count() == 0U, "OS is clean");
    os_shutdown();
    free(b.d);
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
