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
    FILE* x = fopen("task_memory_access.wasm", "rb");
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

static int32_t run_to_result(OsTaskHandle t)
{
    OsValue v;
    uint32_t i;
    for (i = 0; t && i < 50U && os_task_get_state(t) != OS_TASK_DEAD; i++)
        os_schedule();
    if (!t || os_task_get_state(t) != OS_TASK_DEAD || os_task_get_return_value(t, &v) != OS_STATUS_OK)
        return -1;
    return (int32_t)v.value.i32;
}

static uint32_t g_buffer_address;

static m3ApiRawFunction(host_publish)
{
    m3ApiGetArg(uint32_t, address);
    (void)runtime;
    (void)_ctx;
    (void)_mem;
    g_buffer_address = address;
    m3ApiSuccess();
}

int main(void)
{
    Bin b = {0};
    OsTaskHandle t = NULL;
    uint8_t bytes[16];
    uint8_t replacement[16];
    uint32_t expected = 0U;
    uint32_t i;
    int same = 1;

    l = fopen("task_memory_access.log", "w");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(os_host_import_register("env", "host_publish", "v(i)", host_publish) == OS_STATUS_OK, "register env.host_publish");
    ok(load(&b), "load task_memory_access.wasm");
    if (!b.d)
        return 1;

    ok(os_task_create(&t, b.d, b.n, "app_main", "memory", 65536U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK && t,
       "create task");
    os_schedule();
    ok(os_task_get_state(t) == OS_TASK_WAITING && g_buffer_address != 0U, "task published its buffer and sleeps");

    /* Read what the guest initialized, then replace it. */
    ok(os_task_read_memory(t, g_buffer_address, bytes, sizeof(bytes)) == OS_STATUS_OK, "read the guest buffer");
    for (i = 0U; i < sizeof(bytes); ++i)
        same &= bytes[i] == (uint8_t)(i + 1U);
    ok(same, "host sees the guest's initial bytes");
    for (i = 0U; i < sizeof(replacement); ++i)
    {
        replacement[i] = (uint8_t)(100U + i);
        expected += replacement[i];
    }
    ok(os_task_write_memory(t, g_buffer_address, replacement, sizeof(replacement)) == OS_STATUS_OK, "write the guest buffer");

    /* Bounds are checked, including addresses near the 32-bit limit. */
    ok(os_task_read_memory(t, 0xFFFFFFF0U, bytes, 4U) == OS_STATUS_OUT_OF_BOUNDS, "read past linear memory is rejected");
    ok(os_task_write_memory(t, 0xFFFFFFFFU, replacement, 2U) == OS_STATUS_OUT_OF_BOUNDS, "address overflow is rejected");
    ok(os_task_read_memory(t, g_buffer_address, NULL, 4U) == OS_STATUS_INVALID_ARGUMENT, "NULL buffer is rejected");
    ok(os_task_read_memory(t, g_buffer_address, NULL, 0U) == OS_STATUS_OK, "an empty read needs no buffer");
    ok(os_task_read_memory(NULL, 0U, bytes, 1U) == OS_STATUS_INVALID_ARGUMENT, "NULL task is rejected");

    os_tick(10U);
    ok(run_to_result(t) == (int32_t)expected, "guest sees the bytes the host wrote");
    os_task_delete(t);

    ok(os_get_task_count() == 0U, "OS is clean");
    os_shutdown();
    free(b.d);
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
