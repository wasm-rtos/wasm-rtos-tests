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
    FILE* x = fopen("dac_api.wasm", "rb");
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

/*
 * The board: GPIO 25 and 26, and converters in memory. Channel 1 is GPIO
 * 25's own 8-bit converter (3.3 V), channel 0 an external 12-bit chip
 * (2.5 V reference) that is no GPIO pin. What the port was asked is kept.
 */
static uint32_t g_value[2];
static uint8_t g_on[2];
static uint32_t g_starts;
static uint32_t g_stops;
static uint32_t g_writes;
static uint32_t g_pin25_flags = 0xFFFFFFFFU;

static OsStatus gpio_configure(void* context, uint32_t pin, uint32_t flags)
{
    (void)context;
    if (pin == 25U)
        g_pin25_flags = flags;
    return OS_STATUS_OK;
}

static OsStatus gpio_write(void* context, uint32_t pin, uint8_t level)
{
    (void)context;
    (void)pin;
    (void)level;
    return OS_STATUS_OK;
}

static OsStatus gpio_read(void* context, uint32_t pin, uint8_t* out_level)
{
    (void)context;
    (void)pin;
    *out_level = 0U;
    return OS_STATUS_OK;
}

static OsStatus dac_start(void* context, uint32_t channel)
{
    (void)context;
    g_on[channel] = 1U;
    g_starts++;
    return OS_STATUS_OK;
}

static OsStatus dac_write(void* context, uint32_t channel, uint32_t raw)
{
    (void)context;
    g_value[channel] = raw;
    g_writes++;
    return OS_STATUS_OK;
}

static void dac_stop(void* context, uint32_t channel)
{
    (void)context;
    g_on[channel] = 0U;
    g_stops++;
}

static OsTaskHandle start(Bin* b, const char* entry)
{
    OsTaskHandle t = NULL;
    if (os_task_create(&t, b->d, b->n, entry, entry, 0U, OS_TASK_PRIORITY_NORMAL) != OS_STATUS_OK)
        return NULL;
    return t;
}

static void run_until_waiting(OsTaskHandle t)
{
    for (uint32_t i = 0; i < 1000U && os_task_get_state(t) == OS_TASK_READY; i++)
        os_schedule();
}

static int32_t finish(OsTaskHandle t, const char* entry)
{
    OsValue v;
    int32_t result = -100000;
    for (uint32_t i = 0; t && i < 100000U && os_task_get_state(t) != OS_TASK_DEAD; i++)
        if (os_schedule() == OS_STATUS_NO_READY_TASKS)
            os_tick(1U);
    if (t && os_task_get_state(t) == OS_TASK_DEAD && os_task_get_return_value(t, &v) == OS_STATUS_OK)
        result = (int32_t)v.value.i32;
    printf("     %s returned %d\n", entry, (int)result);
    if (t)
        os_task_delete(t);
    return result;
}

int main(void)
{
    static const OsDacInfo own = { 8U, 3300U };
    static const OsDacInfo chip = { 12U, 2500U };
    static const OsDacInfo bad = { 0U, 3300U };
    Bin b = {0};
    OsGpioPort gpio = { NULL, gpio_configure, gpio_write, gpio_read };
    OsDacPort port = { NULL, dac_start, dac_write, dac_stop };
    OsDacPort empty = { NULL, dac_start, NULL, dac_stop };
    OsTaskHandle t = NULL;

    l = fopen("dac_api.log", "w");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load dac_api.wasm");
    if (!b.d)
        return 1;
    ok(os_gpio_port_set(&gpio) == OS_STATUS_OK && os_gpio_add_pin("gpio25", 25U, OS_GPIO_CAN_ALL, 0U) == OS_STATUS_OK &&
           os_gpio_add_pin("gpio26", 26U, OS_GPIO_CAN_ALL, 0U) == OS_STATUS_OK,
       "GPIO pins 25 and 26");

    ok(finish(start(&b, "app_main_try"), "app_main_try") == -13, "without a port dac-open returns -WR_EUNSUPPORTED");
    ok(os_dac_port_set(&empty) == OS_STATUS_INVALID_ARGUMENT, "a port needs write and stop");
    ok(os_dac_port_set(&port) == OS_STATUS_OK, "install the DAC port");
    ok(finish(start(&b, "app_main_try"), "app_main_try") == -13, "without outputs dac-open returns -WR_EUNSUPPORTED");
    ok(os_dac_add_channel("gpio25", 1U, &own) == OS_STATUS_OK && os_dac_add_channel("dac0", 0U, &chip) == OS_STATUS_OK,
       "register GPIO 25's converter and an external chip");
    ok(os_dac_add_channel("gpio25", 3U, &own) == OS_STATUS_BUSY && os_dac_add_channel("x", 1U, &own) == OS_STATUS_BUSY &&
           os_dac_add_channel("y", 4U, &bad) == OS_STATUS_INVALID_ARGUMENT,
       "names and channels are registered once, with a resolution");

    ok(finish(start(&b, "app_main_basic"), "app_main_basic") == 0,
       "properties, raw values, rounded voltages, limits, ownership of the output and its pin, bad flags and handles");
    ok(!g_on[0] && !g_on[1] && g_starts == 2U && g_stops == 2U, "both outputs were switched on and off again");

    t = start(&b, "app_main_hold");
    run_until_waiting(t);
    ok(g_on[1] && g_value[1] == 200U && os_dac_get_owner(1U) == os_task_get_id(t) &&
           os_gpio_get_owner(25U) == os_task_get_id(t),
       "a program holds the output at 200, and with it GPIO 25");
    ok(finish(t, "app_main_hold") == 0 && !g_on[1] && os_dac_get_owner(1U) == 0U && os_gpio_get_owner(25U) == 0U &&
           g_pin25_flags == 0U,
       "when it ends the output is off and the pin back to a plain input");

    ok(finish(start(&b, "app_main_gpio_first"), "app_main_gpio_first") == -22,
       "a pin opened as GPIO keeps its converter from others: -WR_EINUSE");

    free(b.d);
    ok(os_get_task_count() == 0U, "clean OS state");
    os_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
