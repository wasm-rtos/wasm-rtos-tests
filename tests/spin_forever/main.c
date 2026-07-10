#include "os.h"
#include "hal.h"
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
    FILE* x = fopen("spin_forever.wasm", "rb");
    long n;
    if (!x)
        return 0;
    fseek(x, 0, SEEK_END);
    n = ftell(x);
    rewind(x);
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
    OsTaskHandle t = NULL;
    l = fopen("spin_forever.log", "a");
    os_init();
    ok(load(&b), "load spin_forever.wasm");
    if (b.d)
    {
        ok(os_task_create(&t, b.d, b.n, "app_main", "spin_forever", 65536U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK &&
               t,
           "create long-running task");
        if (t)
        {
            ok(os_schedule() == OS_STATUS_OK, "run one scheduler slice");
            ok(os_task_get_state(t) == OS_TASK_READY, "fuel exhaustion returns task to READY");
            ok(os_task_get_run_count(t) == 1U, "one scheduler slice recorded");
            ok(os_task_delete(t) == OS_STATUS_OK, "delete long-running task");
        }
        free(b.d);
    }
    ok(os_get_task_count() == 0U, "no remaining tasks");
    os_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
