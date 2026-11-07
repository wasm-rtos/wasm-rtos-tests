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
    FILE* x = fopen("arduino_dac.wasm", "rb");
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

/* GPIO 25 with an 8-bit converter; the port's calls, in order, for the check. */
static char g_log[256];
static size_t g_log_length;

static void note(const char* text)
{
    size_t length = strlen(text);
    if (g_log_length + length < sizeof(g_log))
    {
        memcpy(g_log + g_log_length, text, length + 1U);
        g_log_length += length;
    }
}

static OsStatus gpio_configure(void* context, uint32_t pin, uint32_t flags)
{
    char text[32];
    (void)context;
    snprintf(text, sizeof(text), "gpio%u:%s ", (unsigned)pin, (flags & OS_GPIO_OUTPUT) ? "out" : "in");
    note(text);
    return OS_STATUS_OK;
}

static OsStatus gpio_write(void* context, uint32_t pin, uint8_t level)
{
    char text[32];
    (void)context;
    snprintf(text, sizeof(text), "set%u=%u ", (unsigned)pin, (unsigned)level);
    note(text);
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
    (void)channel;
    note("on ");
    return OS_STATUS_OK;
}

static OsStatus dac_write(void* context, uint32_t channel, uint32_t raw)
{
    char text[32];
    (void)context;
    (void)channel;
    snprintf(text, sizeof(text), "dac=%u ", (unsigned)raw);
    note(text);
    return OS_STATUS_OK;
}

static void dac_stop(void* context, uint32_t channel)
{
    (void)context;
    (void)channel;
    note("off ");
}

int main(void)
{
    static const char expected[] =
        "gpio25:in on dac=0 dac=128 dac=255 off gpio25:in gpio25:out set25=1 gpio25:in gpio25:in on dac=0 dac=64 off gpio25:in ";
    static const OsDacInfo own = { 8U, 3300U };
    Bin b = {0};
    OsGpioPort gpio = { NULL, gpio_configure, gpio_write, gpio_read };
    OsDacPort port = { NULL, dac_start, dac_write, dac_stop };
    OsTaskHandle t = NULL;
    uint32_t i;

    l = fopen("arduino_dac.log", "w");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load arduino_dac.wasm");
    if (!b.d)
        return 1;
    ok(os_gpio_port_set(&gpio) == OS_STATUS_OK && os_gpio_add_pin("gpio25", 25U, OS_GPIO_CAN_ALL, 0U) == OS_STATUS_OK &&
           os_dac_port_set(&port) == OS_STATUS_OK && os_dac_add_channel("gpio25", 1U, &own) == OS_STATUS_OK,
       "a board with an 8-bit converter on GPIO 25");
    g_log_length = 0U;
    g_log[0] = '\0';
    ok(os_task_create(&t, b.d, b.n, "_start", "sketch", 65536U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK && t,
       "start the sketch");
    for (i = 0; t && i < 100000U && os_task_get_state(t) != OS_TASK_DEAD; i++)
        if (os_schedule() == OS_STATUS_NO_READY_TASKS)
            os_tick(10U);
    ok(t && os_task_get_state(t) == OS_TASK_DEAD && os_task_get_exit_code(t) == 0U, "the sketch ran and exited with 0");
    ok(strcmp(g_log, expected) == 0,
       "analogWrite drove the converter (8 bits, then 12 scaled to 8), digitalWrite took the pin back, dacWrite reopened it");
    if (strcmp(g_log, expected) != 0)
        printf("got:      %s\nexpected: %s\n", g_log, expected);

    if (t)
        os_task_delete(t);
    free(b.d);
    ok(os_get_task_count() == 0U, "clean OS state");
    os_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
