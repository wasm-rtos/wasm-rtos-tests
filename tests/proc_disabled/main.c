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
    FILE* x = fopen("proc_disabled.wasm", "rb");
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
    l = fopen("proc_disabled.log", "a");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(OS_HAS_PROCESSES == 0, "kernel is built without processes");
    ok(load(&b), "load proc_disabled.wasm");
    if (b.d)
    {
        uint32_t i;
        ok(os_task_create(&t, b.d, b.n, NULL, "proc_disabled", 65536U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK && t,
           "program that imports spawn still loads");
        for (i = 0U; t && i < 50U && os_task_get_state(t) != OS_TASK_DEAD; ++i)
            os_schedule();
        ok(t && os_task_get_state(t) == OS_TASK_DEAD, "program finishes");
        ok(t && os_task_get_exit_code(t) == (uint32_t)-13, "spawn returns -WR_EUNSUPPORTED");
        if (t)
            os_task_delete(t);
        free(b.d);
    }
    ok(os_get_task_count() == 0U, "clean OS state");
    os_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
