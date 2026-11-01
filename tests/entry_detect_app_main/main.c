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
    FILE* x = fopen("entry_detect_app_main.wasm", "rb");
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
    l = fopen("entry_detect_app_main.log", "a");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load entry_detect_app_main.wasm");
    if (b.d)
    {
        OsValue value;
        ok(os_task_create(&t, b.d, b.n, NULL, "entry_null", 65536U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK && t,
           "create task with a NULL entry name");
        run_until_dead(t);
        if (t)
        {
            ok(os_task_get_exit_reason(t) == OS_TASK_EXIT_RETURNED, "detected app_main returns");
            ok(os_task_get_return_value(t, &value) == OS_STATUS_OK && value.type == OS_VALUE_TYPE_I32 && value.value.i32 == 5U,
               "detected app_main returned 5");
            ok(os_task_delete(t) == OS_STATUS_OK, "delete NULL-entry task");
        }
        t = NULL;
        ok(os_task_create(&t, b.d, b.n, "", "entry_empty", 65536U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK && t,
           "create task with an empty entry name");
        run_until_dead(t);
        if (t)
        {
            ok(os_task_get_exit_reason(t) == OS_TASK_EXIT_RETURNED, "empty entry name also runs app_main");
            ok(os_task_delete(t) == OS_STATUS_OK, "delete empty-entry task");
        }
        free(b.d);
    }
    ok(os_get_task_count() == 0U, "clean OS state");
    os_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
