#include "os.h"
#include "hal.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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
    FILE* x = fopen("spin_empty_loop.wasm", "rb");
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
    OsTaskHandle first = NULL;
    OsTaskHandle second = NULL;
    uint32_t i;
    /* A loop that is never preempted would hang; fail instead. */
    alarm(30);
    l = fopen("spin_empty_loop.log", "a");
    hal_init();
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load spin_empty_loop.wasm");
    if (b.d)
    {
        ok(os_task_create(&first, b.d, b.n, "app_main", "first", 65536U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK && first,
           "create first empty-loop task");
        ok(os_task_create(&second, b.d, b.n, "app_main", "second", 65536U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK && second,
           "create second empty-loop task");
        for (i = 0; i < 10U; i++)
            ok(os_schedule() == OS_STATUS_OK, "slice returns to the scheduler");
        ok(first && os_task_get_run_count(first) == 5U, "first task got half of the slices");
        ok(second && os_task_get_run_count(second) == 5U, "second task got half of the slices");
        ok(first && os_task_get_state(first) != OS_TASK_DEAD, "first task is still looping");
        ok(os_task_delete(first) == OS_STATUS_OK, "delete first task");
        ok(os_task_delete(second) == OS_STATUS_OK, "delete second task");
        free(b.d);
    }
    ok(os_get_task_count() == 0U, "clean OS state");
    os_shutdown();
    hal_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
