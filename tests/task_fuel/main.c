#include "os.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* The kernel's default slice; the runner builds this test without overriding it. */
#define SLICE_FUEL 10000ULL

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
    FILE* x = fopen("task_fuel.wasm", "rb");
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
        return 0;
    }
    fclose(x);
    return 1;
}

int main(void)
{
    Bin b = {0};
    OsTaskHandle t1 = NULL;
    OsTaskHandle t2 = NULL;
    uint32_t i;
    l = fopen("task_fuel.log", "a");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load task_fuel.wasm");
    ok(os_task_get_fuel_used(NULL) == 0U, "no task, no fuel");
    if (b.d)
    {
        ok(os_task_create(&t1, b.d, b.n, "_start", "first", 65536U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK && t1,
           "create first task");
        ok(os_task_create(&t2, b.d, b.n, "_start", "second", 65536U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK && t2,
           "create second task");
        if (t1 && t2)
        {
            uint64_t before = 0U;
            ok(os_task_get_fuel_used(t1) == 0U, "a task that has not run used no fuel");

            os_schedule();
            before = os_task_get_fuel_used(t1);
            ok(before == SLICE_FUEL, "a slice that runs out spends all of its fuel");

            for (i = 0; i < 1000U && (os_task_get_state(t1) != OS_TASK_DEAD || os_task_get_state(t2) != OS_TASK_DEAD); i++)
                os_schedule();
            ok(os_task_get_state(t1) == OS_TASK_DEAD && os_task_get_state(t2) == OS_TASK_DEAD, "both tasks finish");
            ok(os_task_get_fuel_used(t1) > 50000U, "the loop's work is counted");
            ok(os_task_get_fuel_used(t1) == os_task_get_fuel_used(t2), "the same program spends the same fuel");
            /* 100 slices end at a yield, well before their fuel runs out. */
            ok(os_task_get_fuel_used(t1) + 100U * SLICE_FUEL / 2U < (uint64_t)os_task_get_run_count(t1) * SLICE_FUEL,
               "fuel left at a yield is not counted");
            printf("fuel %llu over %u slices\n", (unsigned long long)os_task_get_fuel_used(t1),
                   (unsigned)os_task_get_run_count(t1));
            ok(os_task_delete(t1) == OS_STATUS_OK && os_task_delete(t2) == OS_STATUS_OK, "delete tasks");
        }
        free(b.d);
    }
    ok(os_get_task_count() == 0U, "clean OS state");
    os_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
