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
    FILE* x = fopen("task_error.wasm", "rb");
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

static OsTaskHandle run(Bin* b, const char* entry)
{
    OsTaskHandle t = NULL;
    uint32_t i;
    if (os_task_create(&t, b->d, b->n, entry, entry, 0U, OS_TASK_PRIORITY_NORMAL) != OS_STATUS_OK)
        return NULL;
    for (i = 0; i < 100U && os_task_get_state(t) != OS_TASK_DEAD; i++)
        os_schedule();
    return t;
}

int main(void)
{
    Bin b = {0};
    OsTaskHandle t = NULL;
    const char* error = NULL;

    l = fopen("task_error.log", "w");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load task_error.wasm");
    if (!b.d)
        return 1;

    t = run(&b, "app_main_ok");
    ok(t && os_task_get_exit_reason(t) == OS_TASK_EXIT_RETURNED && os_task_get_error(t) == NULL,
       "a task that returns has no error");
    os_task_delete(t);

    t = run(&b, "app_main");
    error = os_task_get_error(t);
    printf("     error: %s\n", error ? error : "(none)");
    ok(t && os_task_get_exit_reason(t) == OS_TASK_EXIT_WASM_ERROR, "the task traps");
    ok(error && strstr(error, "unreachable") != NULL, "its error names the trap");
    ok(error && strstr(error, " in parse_header < load_file < app_main") != NULL,
       "and the functions it was in, innermost first");
    /* Another failure must not overwrite the first task's text. */
    {
        OsTaskHandle other = run(&b, "app_main");
        ok(other && os_task_get_error(t) == error, "each task keeps its own error");
        os_task_delete(other);
    }
    os_task_delete(t);
    ok(os_task_get_error(t) == NULL, "a deleted task has none");

    free(b.d);
    ok(os_get_task_count() == 0U, "OS is clean");
    os_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
