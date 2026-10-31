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
    const char* result;
    l = fopen("proc_disabled.log", "a");
    hal_init();
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(OS_HAS_PROCESSES == 0, "kernel is built without processes");
    ok(load(&b), "load proc_disabled.wasm");
    if (b.d)
    {
        ok(os_task_create(&t, b.d, b.n, NULL, "proc_disabled", 65536U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_WASM_ERROR,
           "program that imports spawn is rejected at load");
        ok(t == NULL, "failed creation returns null task");
        ok(strcmp(os_get_last_error_phase(), "check_imports") == 0, "diagnostic phase is check_imports");
        result = os_get_last_error_result();
        printf("result: %s\n", result ? result : "(null)");
        ok(result != NULL && strcmp(result, "missing import wasm-rtos:proc.spawn") == 0,
           "diagnostic names the unavailable process import");
        free(b.d);
    }
    ok(os_get_task_count() == 0U, "clean OS state");
    os_shutdown();
    hal_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
