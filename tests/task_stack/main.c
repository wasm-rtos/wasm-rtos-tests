#include "os.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    FILE* x = fopen("task_stack.wasm", "rb");
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

/* (module (func (export "app_main") (result i32) i32.const 42)) plus an optional custom section. */
static const uint8_t plain_module[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x05, 0x01, 0x60, 0x00, 0x01, 0x7f,
    0x03, 0x02, 0x01, 0x00,
    0x07, 0x0c, 0x01, 0x08, 'a', 'p', 'p', '_', 'm', 'a', 'i', 'n', 0x00, 0x00,
    0x0a, 0x06, 0x01, 0x04, 0x00, 0x41, 0x2a, 0x0b
};

/* The same module with a wasm-rtos.app section that is not whole records. */
static const uint8_t malformed_module[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x05, 0x01, 0x60, 0x00, 0x01, 0x7f,
    0x03, 0x02, 0x01, 0x00,
    0x07, 0x0c, 0x01, 0x08, 'a', 'p', 'p', '_', 'm', 'a', 'i', 'n', 0x00, 0x00,
    0x0a, 0x06, 0x01, 0x04, 0x00, 0x41, 0x2a, 0x0b,
    0x00, 0x11, 0x0d, 'w', 'a', 's', 'm', '-', 'r', 't', 'o', 's', '.', 'a', 'p', 'p', 0x01, 0x00, 0x10
};

static OsTaskMemoryUsage usage_of(OsTaskHandle t)
{
    OsTaskMemoryUsage u = {0};
    os_task_get_memory_usage(t, &u);
    return u;
}

/* Stack size without the interpreter's guard slots. */
static uint32_t stack_of(OsTaskHandle t)
{
    OsTaskMemoryUsage u = usage_of(t);
    return u.stack_bytes & ~63U;
}

static OsTaskHandle recursion(Bin* b, uint32_t depth, uint32_t stack_size)
{
    OsTaskHandle t = NULL;
    OsValue arg;
    arg.type = OS_VALUE_TYPE_I32;
    arg.value.i32 = depth;
    if (os_task_create_with_args(&t, b->d, b->n, "app_main_recurse", &arg, 1U, "recurse", stack_size,
                                 OS_TASK_PRIORITY_NORMAL) != OS_STATUS_OK)
        return NULL;
    return t;
}

int main(void)
{
    Bin b = {0};
    OsTaskHandle t = NULL;
    uint32_t tiny_peak = 0U;
    uint32_t peak_10 = 0U;
    uint32_t peak_40 = 0U;

    l = fopen("task_stack.log", "w");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load task_stack.wasm");
    if (!b.d)
        return 1;

    /* Where the size comes from. */
    ok(os_task_create(&t, b.d, b.n, "app_main", "own", 0U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK && t,
       "create without a size");
    ok(stack_of(t) == 4096U, "the program's own 4 KiB request applies (an unknown key is skipped)");
    os_task_delete(t);
    ok(os_task_create(&t, b.d, b.n, "app_main", "explicit", 16384U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK && t,
       "create with 16 KiB");
    ok(stack_of(t) == 16384U, "the creator's size wins over the program's");
    os_task_delete(t);
    ok(os_task_create(&t, (uint8_t*)plain_module, sizeof(plain_module), NULL, "plain", 0U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK && t,
       "create a module without settings");
    ok(stack_of(t) == 8192U, "it gets the 8 KiB default");
    os_task_delete(t);
#if OS_HAS_PROCESSES
    ok(os_task_spawn(&t, b.d, b.n, NULL, 0U, 0U, 12288U) == OS_STATUS_OK && t && stack_of(t) == 12288U,
       "spawn with a 12 KiB stack");
    os_task_delete(t);
    ok(os_task_spawn(&t, b.d, b.n, NULL, 0U, 0U, 0U) == OS_STATUS_OK && t && stack_of(t) == 4096U,
       "spawn without a size uses the program's request");
    os_task_delete(t);
#endif

    /* A section that is not whole records fails the load. */
    t = NULL;
    ok(os_task_create(&t, (uint8_t*)malformed_module, sizeof(malformed_module), NULL, "bad", 0U, OS_TASK_PRIORITY_NORMAL) ==
           OS_STATUS_WASM_ERROR && !t,
       "a malformed wasm-rtos.app section is rejected");
    ok(strcmp(os_get_last_error_phase(), "app_metadata") == 0, "the error names the metadata");

    /* The high-water mark grows with use and outlives the task. */
    ok(os_task_create(&t, b.d, b.n, "app_main", "tiny", 0U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK && t, "create a trivial task");
    run_to_result(t);
    tiny_peak = usage_of(t).stack_peak_bytes;
    os_task_delete(t);
    t = recursion(&b, 10U, 0U);
    ok(run_to_result(t) == 10, "recurse 10 deep");
    peak_10 = usage_of(t).stack_peak_bytes;
    os_task_delete(t);
    t = recursion(&b, 40U, 0U);
    ok(run_to_result(t) == 40, "recurse 40 deep");
    peak_40 = usage_of(t).stack_peak_bytes;
    ok(os_task_get_state(t) == OS_TASK_DEAD && peak_40 != 0U, "a finished task still reports its peak");
    os_task_delete(t);
    printf("stack peak: trivial=%u depth10=%u depth40=%u\n", tiny_peak, peak_10, peak_40);
    if (l)
        fprintf(l, "MEASURED stack peak trivial=%u depth10=%u depth40=%u\n", tiny_peak, peak_10, peak_40);
    ok(tiny_peak > 0U && tiny_peak < peak_10 && peak_10 < peak_40, "deeper calls use more stack");
    ok(peak_40 - peak_10 > 2U * (peak_10 - tiny_peak), "use grows with depth");

    /* Overflow traps instead of corrupting memory. */
    t = recursion(&b, 100000U, 0U);
    ok(t != NULL, "create a task that recurses far beyond its 4 KiB");
    run_to_result(t);
    ok(os_task_get_exit_reason(t) == OS_TASK_EXIT_WASM_ERROR, "it traps");
    ok(os_get_last_error_result() != NULL && strstr(os_get_last_error_result(), "stack overflow") != NULL,
       "the trap is a stack overflow");
    ok(usage_of(t).stack_peak_bytes <= 4096U, "it never wrote past its stack");
    os_task_delete(t);

    ok(os_get_task_count() == 0U, "OS is clean");
    os_shutdown();
    free(b.d);
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
