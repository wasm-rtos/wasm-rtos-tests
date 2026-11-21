#include "os.h"
#include "hw/hw_driver.h"

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
static uint32_t g_closed;

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
    FILE* x = fopen("driver_api.wasm", "rb");
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

/* The host's own driver: wasm-rtos:kernel/echo. */
extern const HwDriver echo_driver;

static HW_HOST_FUNCTION(echo_twice)
{
    HW_HOST_RETURNS(int32_t);
    HW_HOST_ARG(int32_t, value);
    HW_ABI_UNUSED();
    HW_HOST_RETURN(value * 2);
}

static HW_HOST_FUNCTION(echo_open)
{
    HW_HOST_RETURNS(int32_t);
    HW_ABI_UNUSED();
    HW_HOST_RETURN(hw_driver_handle_new(&echo_driver, 7U));
}

static HW_HOST_FUNCTION(echo_id)
{
    HW_HOST_RETURNS(int32_t);
    HW_HOST_ARG(int32_t, handle);
    uint32_t id = 0U;
    HW_ABI_UNUSED();
    HW_HOST_RETURN(hw_driver_handle_get(&echo_driver, handle, &id) ? (int32_t)id : -WR_EBADH);
}

static void echo_close(uint32_t id)
{
    g_closed += id;
}

static const HwDriverCall echo_calls[] = {
    { "twice", "i(i)", echo_twice, 0U },
    { "open", "i()", echo_open, 0U },
    { "id", "i(i)", echo_id, 0U },
};

const HwDriver echo_driver = {
    .interface = "echo",
    .calls = echo_calls,
    .call_count = 3U,
    .close = echo_close,
};

static int32_t run(Bin* b, const char* entry)
{
    OsTaskHandle t = NULL;
    uint32_t i;
    int32_t code = -1000;
    if (os_task_create(&t, b->d, b->n, entry, entry, 65536U, OS_TASK_PRIORITY_NORMAL) != OS_STATUS_OK || !t)
        return -2000;
    for (i = 0U; i < 50U && os_task_get_state(t) != OS_TASK_DEAD; ++i)
        os_schedule();
    if (os_task_get_state(t) == OS_TASK_DEAD)
        code = (int32_t)os_task_get_exit_code(t);
    os_task_delete(t);
    return code;
}

int main(void)
{
    Bin b = {0};
    l = fopen("driver_api.log", "a");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load driver_api.wasm");
    if (b.d)
    {
        ok(run(&b, "app_main_absent") == 1,
           "a program that imports interfaces no driver offers loads; their calls are unsupported");
        ok(hw_driver_add(&echo_driver) == OS_STATUS_OK, "add the host's own driver");
        ok(hw_driver_add(&echo_driver) == OS_STATUS_OK, "adding it again does nothing");
        ok(run(&b, "app_main_custom") == 42, "a program calls the driver and uses its handle");
        ok(g_closed == 7U, "closing the handle reached the driver");
        free(b.d);
    }
    ok(os_get_task_count() == 0U, "clean OS state");
    os_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
