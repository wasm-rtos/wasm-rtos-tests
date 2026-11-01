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
    FILE* x = fopen("preempt_memory_grow.wasm", "rb");
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

int main(void)
{
    Bin b = {0};
    OsTaskHandle t = NULL;
    OsValue value;
    uint32_t slices = 0U;
    l = fopen("preempt_memory_grow.log", "a");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(OS_TASK_SLICE_FUEL == 1ULL, "kernel preempts after every unit of work");
    ok(load(&b), "load preempt_memory_grow.wasm");
    if (b.d)
    {
        ok(os_task_create(&t, b.d, b.n, NULL, "grow", 65536U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK && t,
           "create task");
        while (t && os_task_get_state(t) != OS_TASK_DEAD)
        {
            if (os_schedule() == OS_STATUS_WASM_ERROR)
                break;
            ++slices;
        }
        if (t)
        {
            printf("slices: %u\n", slices);
            ok(os_task_get_exit_reason(t) == OS_TASK_EXIT_RETURNED, "task returns without a trap");
            ok(os_task_get_return_value(t, &value) == OS_STATUS_OK && value.value.i32 == 755949116U,
               "checksum matches an uninterrupted run");
            ok(os_task_delete(t) == OS_STATUS_OK, "delete task");
        }
        free(b.d);
    }
    ok(os_get_task_count() == 0U, "clean OS state");
    os_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
