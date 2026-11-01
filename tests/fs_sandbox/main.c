#include "os.h"
#include "fs.h"
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
    FILE* x = fopen("fs_sandbox.wasm", "rb");
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

static uint32_t run_program(Bin* b, const char* name)
{
    OsTaskHandle t = NULL;
    uint32_t i;
    uint32_t code = 999U;
    ok(os_task_create(&t, b->d, b->n, NULL, name, 65536U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK && t,
       "create WASI program");
    for (i = 0; t && i < 200U && os_task_get_state(t) != OS_TASK_DEAD; i++)
        os_schedule();
    if (t)
    {
        ok(os_task_get_state(t) == OS_TASK_DEAD, "program finishes");
        code = os_task_get_exit_code(t);
        printf("exit code: %u\n", code);
        os_task_delete(t);
    }
    return code;
}

int main(void)
{
    Bin b = {0};
    l = fopen("fs_sandbox.log", "a");
    ok(os_init() == OS_STATUS_OK, "initialize OS with nothing mounted");
    ok(load(&b), "load fs_sandbox.wasm");
    if (b.d)
    {
        ok(run_program(&b, "fs_sandbox") == 0U, "program cannot open files from the host working directory");
        free(b.d);
    }
    os_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
