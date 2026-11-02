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
    FILE* x = fopen("bus_spi.wasm", "rb");
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

/* Pins of the board: chip selects on pins 5 ("cs-a") and 6 ("cs-b", which the board marks inverted). */
#define CS_A 5U
#define CS_B 6U

static uint32_t g_pin_flags[8];
static uint8_t g_pin_level[8];

static OsStatus fake_configure(void* context, uint32_t pin, uint32_t flags)
{
    (void)context;
    g_pin_flags[pin] = flags;
    if (flags & OS_GPIO_OUTPUT)
        g_pin_level[pin] = (flags & OS_GPIO_INIT_HIGH) ? 1U : 0U;
    else
        g_pin_level[pin] = 1U; /* the pull-up */
    return OS_STATUS_OK;
}

static OsStatus fake_write(void* context, uint32_t pin, uint8_t level)
{
    (void)context;
    g_pin_level[pin] = level;
    return OS_STATUS_OK;
}

static OsStatus fake_read(void* context, uint32_t pin, uint8_t* out_level)
{
    (void)context;
    *out_level = g_pin_level[pin];
    return OS_STATUS_OK;
}

/*
 * The bus: whichever chip select is low picks the device. A flash on cs-a
 * answers 0x9F with its JEDEC id; the device on cs-b sends back each byte
 * inverted.
 */
static uint32_t g_mode;
static uint32_t g_frequency;
static uint32_t g_delays;
static uint8_t g_selected_both;

static OsStatus fake_transaction(void* context, uint32_t bus, uint32_t mode, uint32_t frequency_hz,
                                 const OsSpiOp* ops, uint32_t count)
{
    static const uint8_t jedec[3] = { 0xEF, 0x40, 0x18 };
    uint8_t flash = g_pin_level[CS_A] == 0U;
    uint8_t echo = g_pin_level[CS_B] == 0U;
    uint8_t command = 0;
    uint32_t index;
    (void)context;
    if (bus != 0U)
        return OS_STATUS_INVALID_ARGUMENT;
    g_mode = mode;
    g_frequency = frequency_hz;
    g_selected_both |= flash && echo;
    for (index = 0; index < count; index++)
    {
        const OsSpiOp* op = &ops[index];
        uint32_t byte;
        if (op->kind == OS_SPI_DELAY_US)
        {
            g_delays += op->length;
            continue;
        }
        for (byte = 0; byte < op->length; byte++)
        {
            uint8_t out = op->write ? op->write[byte] : 0U;
            uint8_t in = 0xFFU;
            if (flash && op->kind == OS_SPI_WRITE)
                command = out;
            if (flash && op->kind == OS_SPI_READ && command == 0x9FU && byte < 3U)
                in = jedec[byte];
            if (echo)
                in = (uint8_t)~out;
            if (op->read)
                op->read[byte] = in;
        }
    }
    return OS_STATUS_OK;
}

static uint32_t g_traces;
static OsBusTrace g_last_trace;

static void trace(void* context, const OsBusTrace* record)
{
    (void)context;
    g_traces++;
    g_last_trace = *record;
}

static OsTaskHandle start(Bin* b, const char* entry)
{
    OsTaskHandle t = NULL;
    if (os_task_create(&t, b->d, b->n, entry, entry, 0U, OS_TASK_PRIORITY_NORMAL) != OS_STATUS_OK)
        return NULL;
    return t;
}

static int32_t finish(OsTaskHandle t, const char* entry)
{
    OsValue v;
    uint32_t i;
    int32_t result = -100000;
    for (i = 0; t && i < 1000U && os_task_get_state(t) != OS_TASK_DEAD; i++)
    {
        if (os_schedule() == OS_STATUS_NO_READY_TASKS)
            os_tick(10U);
    }
    if (t && os_task_get_state(t) == OS_TASK_DEAD && os_task_get_return_value(t, &v) == OS_STATUS_OK)
        result = (int32_t)v.value.i32;
    printf("     %s returned %d\n", entry, (int)result);
    if (t)
        os_task_delete(t);
    return result;
}

int main(void)
{
    Bin b = {0};
    OsGpioPort gpio = { NULL, fake_configure, fake_write, fake_read };
    OsSpiPort port = { NULL, fake_transaction };
    OsTaskHandle flash = NULL;
    uint32_t i;

    /* Chip select lines have pull-ups on the board: nothing is selected until driven low. */
    memset(g_pin_level, 1, sizeof(g_pin_level));
    l = fopen("bus_spi.log", "w");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load bus_spi.wasm");
    if (!b.d)
        return 1;

    ok(finish(start(&b, "app_main_try"), "app_main_try") == -13, "without a port spi-open returns -WR_EUNSUPPORTED");
    ok(os_gpio_port_set(&gpio) == OS_STATUS_OK && os_gpio_add_pin("cs-a", CS_A, OS_GPIO_CAN_ALL, 0U) == OS_STATUS_OK &&
           os_gpio_add_pin("cs-b", CS_B, OS_GPIO_CAN_ALL, OS_GPIO_ACTIVE_LOW) == OS_STATUS_OK,
       "register the chip select pins");
    ok(os_spi_port_set(&port) == OS_STATUS_OK && os_spi_add_bus("spi0", 0U, 20000000U) == OS_STATUS_OK,
       "install the SPI port and bus spi0, 20 MHz at most");
    os_bus_trace_set(trace, NULL);

    flash = start(&b, "app_main_flash");
    for (i = 0; i < 100U && os_task_get_state(flash) != OS_TASK_WAITING; i++)
        os_schedule();
    ok(g_mode == 3U && g_frequency == 1000000U, "mode 3 at the 1 MHz the program asked for");
    ok(g_pin_flags[CS_A] == (OS_GPIO_OUTPUT | OS_GPIO_INIT_HIGH) && g_pin_level[CS_A] == 1U,
       "chip select is an output that idles high");
    ok(os_spi_get_owner(0U, CS_A) == os_task_get_id(flash) && os_gpio_get_owner(CS_A) == os_task_get_id(flash),
       "the program owns its chip select");
    ok(finish(start(&b, "app_main_echo"), "app_main_echo") == 0,
       "a second device shares the bus; its chip select goes low although the board inverts it; in-place transfer and a delay");
    ok(g_delays == 5U && !g_selected_both, "the delay reached the port; one chip select at a time");
    ok(finish(flash, "app_main_flash") == 0xEF4018, "the flash answered its JEDEC id");
    ok(g_pin_flags[CS_A] == 0U && os_spi_get_owner(0U, CS_A) == 0U, "chip select is given back when the program ends");
    ok(g_traces == 2U && g_last_trace.kind == OS_BUS_SPI && g_last_trace.device == CS_B && g_last_trace.count == 2U,
       "the trace names the chip select pin");

    ok(finish(start(&b, "app_main_whole_bus"), "app_main_whole_bus") == 0xEF4018,
       "a program can take the whole bus and drive chip select itself");
    ok(g_frequency == 20000000U && g_last_trace.device == OS_BUS_NO_CS, "a faster request gets the bus maximum");

    ok(finish(start(&b, "app_main_errors"), "app_main_errors") == 0,
       "unknown bus and pin, bad mode, a pin in use, op counts and kinds, long delay, bad pointer, not waitable");

    os_bus_trace_set(NULL, NULL);
    free(b.d);
    ok(os_get_task_count() == 0U, "clean OS state");
    os_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
