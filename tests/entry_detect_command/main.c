#include "os.h"
#include "hal.h"
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
    FILE* x = fopen("entry_detect_command.wasm", "rb");
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

static void run_until_dead(OsTaskHandle t)
{
    uint32_t i;
    for (i = 0; t && i < 50U && os_task_get_state(t) != OS_TASK_DEAD; i++)
        os_schedule();
}

int main(void)
{
    Bin b = {0};
    OsTaskHandle t = NULL;
    l = fopen("entry_detect_command.log", "a");
    hal_init();
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load entry_detect_command.wasm");
    if (b.d)
    {
        ok(os_task_create(&t, b.d, b.n, NULL, "entry_detect_command", 65536U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK && t,
           "create WASI command without naming the entry point");
        run_until_dead(t);
        if (t)
        {
            ok(os_task_get_state(t) == OS_TASK_DEAD, "task reaches DEAD");
            ok(os_task_get_exit_code(t) == 3U, "main return value becomes exit code 3");
            ok(os_task_delete(t) == OS_STATUS_OK, "delete task");
        }
        free(b.d);
    }
    ok(os_get_task_count() == 0U, "clean OS state");
    os_shutdown();
    hal_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
