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
    FILE* x = fopen("custom_page_size.wasm", "rb");
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
    OsTaskMemoryUsage usage = {0};
    uint32_t i;

    l = fopen("custom_page_size.log", "w");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load custom_page_size.wasm (1-byte pages, 4 KiB C stack)");
    if (!b.d)
        return 1;

    ok(os_task_create(&t, b.d, b.n, "app_main", "small", 0U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK && t,
       "a module with 1-byte pages loads");
    ok(os_task_get_memory_usage(t, &usage) == OS_STATUS_OK && usage.linear_memory_bytes < 16384U,
       "its linear memory is a few KiB, not 64 KiB pages");
    printf("linear memory at start: %u bytes\n", usage.linear_memory_bytes);
    if (l)
        fprintf(l, "MEASURED linear memory at start: %u bytes\n", usage.linear_memory_bytes);
    for (i = 0; t && i < 1000U && os_task_get_state(t) != OS_TASK_DEAD; i++)
        os_schedule();
    ok(t && os_task_get_state(t) == OS_TASK_DEAD && os_task_get_exit_reason(t) != OS_TASK_EXIT_WASM_ERROR,
       "it runs to the end");
    ok(t && os_task_get_exit_code(t) == 0U,
       "memory.size and memory.grow count bytes, grown memory is zeroed, memory passes 64 KiB");
    if (t && os_task_get_exit_code(t) != 0U)
        printf("guest check %u failed\n", os_task_get_exit_code(t));
    os_task_delete(t);

    ok(os_get_task_count() == 0U, "OS is clean");
    os_shutdown();
    free(b.d);
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
