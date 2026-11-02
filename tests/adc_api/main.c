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
    FILE* x = fopen("adc_api.wasm", "rb");
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

/* Channel 6: a potentiometer at mid travel. Channel 9: a battery divider whose port reports too much. */
static OsStatus adc_read(void* context, uint32_t channel, uint32_t* out_raw)
{
    (void)context;
    *out_raw = channel == 6U ? 2048U : 5000U;
    return OS_STATUS_OK;
}

/* Only the battery has a calibration curve in this port. */
static OsStatus adc_read_mv(void* context, uint32_t channel, uint32_t* out_mv)
{
    (void)context;
    if (channel != 9U)
        return OS_STATUS_UNSUPPORTED;
    *out_mv = 3987U;
    return OS_STATUS_OK;
}

static int32_t run(Bin* b, const char* entry)
{
    OsTaskHandle t = NULL;
    OsValue v;
    uint32_t i;
    int32_t result = -100000;
    if (os_task_create(&t, b->d, b->n, entry, entry, 0U, OS_TASK_PRIORITY_NORMAL) != OS_STATUS_OK)
        return result;
    for (i = 0; i < 1000U && os_task_get_state(t) != OS_TASK_DEAD; i++)
        if (os_schedule() == OS_STATUS_NO_READY_TASKS)
            os_tick(1U);
    if (os_task_get_state(t) == OS_TASK_DEAD && os_task_get_return_value(t, &v) == OS_STATUS_OK)
        result = (int32_t)v.value.i32;
    printf("     %s returned %d\n", entry, (int)result);
    os_task_delete(t);
    return result;
}

int main(void)
{
    Bin b = {0};
    OsAdcPort plain = { NULL, adc_read, NULL };
    OsAdcPort calibrated = { NULL, adc_read, adc_read_mv };
    OsAdcPort broken = { NULL, NULL, NULL };
    OsAdcInfo pot = { 12U, 3300U };
    OsAdcInfo battery = { 10U, 6600U };
    OsAdcInfo bad = { 0U, 3300U };

    l = fopen("adc_api.log", "w");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load adc_api.wasm");
    if (!b.d)
        return 1;

    ok(run(&b, "app_main_try") == -13, "without a port adc-open returns -WR_EUNSUPPORTED");
    ok(os_adc_port_set(&broken) == OS_STATUS_INVALID_ARGUMENT && os_adc_port_set(&plain) == OS_STATUS_OK,
       "a port needs read");
    ok(run(&b, "app_main_try") == -13, "without channels too");
    ok(os_adc_add_channel("a0", 6U, &pot) == OS_STATUS_OK && os_adc_add_channel("battery", 9U, &battery) == OS_STATUS_OK,
       "register a potentiometer and a battery divider");
    ok(os_adc_add_channel("a0", 7U, &pot) == OS_STATUS_BUSY && os_adc_add_channel("x", 6U, &pot) == OS_STATUS_BUSY &&
           os_adc_add_channel("bad", 8U, &bad) == OS_STATUS_INVALID_ARGUMENT,
       "names and numbers once; a channel has a resolution");
    ok(run(&b, "app_main_read") == 0, "properties, shared reads, millivolts by scale, clamping, errors");
    ok(run(&b, "app_main_calibrated") == 6600, "without read_mv the battery is scaled: 1023 of 1023 is 6600 mV");
    ok(os_adc_port_set(&calibrated) == OS_STATUS_OK && run(&b, "app_main_calibrated") == 3987,
       "a port's calibration wins");
    ok(run(&b, "app_main_read") == 0, "channels the port does not calibrate are still scaled by the kernel");

    free(b.d);
    ok(os_get_task_count() == 0U, "clean OS state");
    os_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
