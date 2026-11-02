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
    FILE* x = fopen("arduino_core.wasm", "rb");
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

/* The board: a button on pin 0 (pull-up), the LED on pin 2, an LM75 at 0x48, an SPI device that inverts bytes. */
static uint32_t g_flags[8];
/* Every way pin 0 was configured, ORed: the sketch's mode survives its exit. */
static uint32_t g_pin0_configured;
static uint8_t g_level[8];
static char g_led_history[16];
static uint32_t g_led_changes;
static char g_output[2048];
static size_t g_output_length;
static uint32_t g_i2c_shapes;
static uint32_t g_spi_mode = 99;
static uint32_t g_spi_frequency;

static OsStatus gpio_configure(void* context, uint32_t pin, uint32_t flags)
{
    (void)context;
    g_flags[pin] = flags;
    if (pin == 0U)
        g_pin0_configured |= flags;
    if (flags & OS_GPIO_OUTPUT)
        g_level[pin] = (flags & OS_GPIO_INIT_HIGH) ? 1U : 0U;
    else
        g_level[pin] = (flags & OS_GPIO_PULL_UP) ? 1U : 0U;
    return OS_STATUS_OK;
}

static OsStatus gpio_write(void* context, uint32_t pin, uint8_t level)
{
    (void)context;
    g_level[pin] = level;
    if (pin == 2U && g_led_changes + 1 < sizeof(g_led_history))
        g_led_history[g_led_changes++] = (char)('0' + level);
    return OS_STATUS_OK;
}

static OsStatus gpio_read(void* context, uint32_t pin, uint8_t* out_level)
{
    (void)context;
    *out_level = g_level[pin];
    return OS_STATUS_OK;
}

static OsStatus i2c_transaction(void* context, uint32_t bus, uint32_t address, uint32_t frequency_hz,
                                const OsI2cOp* ops, uint32_t count)
{
    (void)context;
    (void)bus;
    (void)frequency_hz;
    if (address != 0x48U)
        return OS_STATUS_NACK;
    /* Exactly one write of the pointer, then one read of two bytes. */
    if (count == 2U && ops[0].kind == OS_I2C_WRITE && ops[0].length == 1U && ops[0].data[0] == 0U &&
        ops[1].kind == OS_I2C_READ && ops[1].length == 2U)
    {
        g_i2c_shapes++;
        ops[1].data[0] = 0x19;
        ops[1].data[1] = 0x80;
        return OS_STATUS_OK;
    }
    return OS_STATUS_BUS_ERROR;
}

static OsStatus spi_transaction(void* context, uint32_t bus, uint32_t mode, uint32_t frequency_hz,
                                const OsSpiOp* ops, uint32_t count)
{
    uint32_t index;
    uint32_t byte;
    (void)context;
    (void)bus;
    g_spi_mode = mode;
    g_spi_frequency = frequency_hz;
    for (index = 0; index < count; index++)
        for (byte = 0; byte < ops[index].length; byte++)
            if (ops[index].read)
                ops[index].read[byte] = (uint8_t)~(ops[index].write ? ops[index].write[byte] : 0U);
    return OS_STATUS_OK;
}

static void capture(void* context, uint32_t task_id, uint32_t fd, const void* data, uint32_t size)
{
    (void)context;
    (void)task_id;
    (void)fd;
    if (g_output_length + size < sizeof(g_output))
    {
        memcpy(g_output + g_output_length, data, size);
        g_output_length += size;
    }
}

int main(void)
{
    static const char expected[] =
        "hello\r\nTEMP42\r\n3.142\r\nFF\r\n-12\r\n7-x\npadded|4\r\n127\r\n255\r\n"
        "temperature 25.50\r\nno device: 2\r\nspi 0xA5 0xEDCB\r\nslept enough, presses 1\r\n"
        "temperature 25.50\r\nno device: 2\r\nspi 0xA5 0xEDCB\r\nslept enough, presses 1\r\n";
    Bin b = {0};
    OsGpioPort gpio = { NULL, gpio_configure, gpio_write, gpio_read };
    OsI2cPort i2c = { NULL, i2c_transaction };
    OsSpiPort spi = { NULL, spi_transaction };
    OsOutputPort output = { NULL, capture };
    OsTaskHandle t = NULL;
    uint8_t pressed = 0;
    uint32_t i;

    l = fopen("arduino_core.log", "w");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load arduino_core.wasm");
    if (!b.d)
        return 1;
    ok(os_gpio_port_set(&gpio) == OS_STATUS_OK && os_gpio_add_pin("gpio0", 0U, OS_GPIO_CAN_ALL, 0U) == OS_STATUS_OK &&
           os_gpio_add_pin("led", 2U, OS_GPIO_CAN_ALL, 0U) == OS_STATUS_OK && os_i2c_port_set(&i2c) == OS_STATUS_OK &&
           os_i2c_add_bus("i2c0", 0U, 400000U) == OS_STATUS_OK && os_spi_port_set(&spi) == OS_STATUS_OK &&
           os_spi_add_bus("spi0", 0U, 20000000U) == OS_STATUS_OK && os_output_port_set(&output) == OS_STATUS_OK,
       "a board with a button, an LED, i2c0 and spi0");

    ok(os_task_create(&t, b.d, b.n, "_start", "sketch", 65536U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK && t,
       "start the sketch");
    for (i = 0; t && i < 100000U && os_task_get_state(t) != OS_TASK_DEAD; i++)
    {
        if (os_schedule() == OS_STATUS_NO_READY_TASKS)
        {
            /* The first delay: press the button, as an interrupt handler would report it. */
            if (!pressed && os_task_get_state(t) == OS_TASK_WAITING)
            {
                pressed = 1;
                g_level[0] = 0U;
                os_gpio_edge(0U, OS_GPIO_EDGE_FALLING);
            }
            os_tick(10U);
        }
    }
    ok(t && os_task_get_state(t) == OS_TASK_DEAD && os_task_get_exit_reason(t) == OS_TASK_EXIT_EXPLICIT &&
           os_task_get_exit_code(t) == 0U,
       "setup runs once, loop twice, and the sketch exits with 0");
    g_output[g_output_length] = '\0';
    ok(strcmp(g_output, expected) == 0, "Serial printed text, numbers, Strings and printf as Arduino does");
    if (strcmp(g_output, expected) != 0)
        printf("got %zu bytes:\n%s\nexpected %zu bytes:\n%s\n", strlen(g_output), g_output, strlen(expected), expected);
    ok(strcmp(g_led_history, "10") == 0, "digitalWrite drove LED_BUILTIN, the profile's \"led\"");
    ok(g_pin0_configured == (OS_GPIO_PULL_UP | OS_GPIO_EDGE_FALLING), "INPUT_PULLUP and a FALLING interrupt on pin 0");
    ok(g_i2c_shapes == 2U, "endTransmission(false) and requestFrom made one write-read transaction each round");
    ok(g_spi_mode == 0U && g_spi_frequency == 8000000U, "SPISettings reached the bus: mode 0 at 8 MHz");
    ok(os_spi_get_owner(0U, OS_BUS_NO_CS) == 0U && os_gpio_get_owner(2U) == 0U, "the bus and pins are free after exit");

    if (t)
        os_task_delete(t);
    free(b.d);
    ok(os_get_task_count() == 0U, "clean OS state");
    os_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
