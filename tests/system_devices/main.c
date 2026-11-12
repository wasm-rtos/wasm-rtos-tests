#include "os.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Programs that hold every kind of device are saved, the hardware loses
 * everything as in deep sleep, and the system is restored: every device is
 * set up again as it was (the fake hardware below ends in the same state),
 * and every program ends with the results of a run never stopped. The
 * button a program waits on is a wake source, and its press after the
 * sleep reaches the program. Devices are found by name, so a firmware that
 * registers them in another order serves too. A device the firmware no
 * longer has, or that cannot be set up, ends only the program that held it,
 * and frees what it held; a display that loses its picture in the sleep
 * blocks saving.
 */

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
    FILE* x = fopen("system_devices.wasm", "rb");
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

/* ---- The hardware: what the ports were last told; a deep sleep clears it. */

typedef struct
{
    uint32_t gpio_flags[8];
    uint8_t gpio_level[8];
    uint32_t pwm_frequency[8];
    uint32_t pwm_duty[8];
    uint8_t pwm_on[8];
    uint32_t dac[2];
    uint8_t dac_on[2];
    uint32_t counter_mode[2];
    int32_t count[2];
    uint32_t pulse_flags[8];
    uint32_t uart_baud[2];
    uint32_t uart_format[2];
    uint8_t brightness[2];
} Hardware;

static Hardware g_hw;
/* What went out on the serial line: the wire, not the chip, so the sleep keeps it. */
static uint8_t g_tx[256];
static uint32_t g_tx_count;
/* The PWM port refuses to start after the sleep, when the test says so. */
static int g_pwm_broken;

static OsStatus gpio_configure(void* c, uint32_t pin, uint32_t flags)
{
    (void)c;
    g_hw.gpio_flags[pin] = flags;
    g_hw.gpio_level[pin] = (flags & OS_GPIO_INIT_HIGH) != 0U;
    return OS_STATUS_OK;
}

static OsStatus gpio_write(void* c, uint32_t pin, uint8_t level)
{
    (void)c;
    g_hw.gpio_level[pin] = level;
    return OS_STATUS_OK;
}

static OsStatus gpio_read(void* c, uint32_t pin, uint8_t* level)
{
    (void)c;
    *level = g_hw.gpio_level[pin];
    return OS_STATUS_OK;
}

static OsStatus pwm_start(void* c, uint32_t pin, uint32_t hz, uint32_t duty, uint32_t* out_hz)
{
    (void)c;
    if (g_pwm_broken)
        return OS_STATUS_ERROR;
    g_hw.pwm_frequency[pin] = hz;
    g_hw.pwm_duty[pin] = duty;
    g_hw.pwm_on[pin] = 1;
    *out_hz = hz;
    return OS_STATUS_OK;
}

static OsStatus pwm_duty(void* c, uint32_t pin, uint32_t duty)
{
    (void)c;
    g_hw.pwm_duty[pin] = duty;
    return OS_STATUS_OK;
}

static OsStatus pwm_frequency(void* c, uint32_t pin, uint32_t hz, uint32_t* out_hz)
{
    (void)c;
    g_hw.pwm_frequency[pin] = hz;
    *out_hz = hz;
    return OS_STATUS_OK;
}

static void pwm_stop(void* c, uint32_t pin)
{
    (void)c;
    g_hw.pwm_on[pin] = 0;
}

static OsStatus dac_start(void* c, uint32_t channel)
{
    (void)c;
    g_hw.dac_on[channel] = 1;
    return OS_STATUS_OK;
}

static OsStatus dac_write(void* c, uint32_t channel, uint32_t raw)
{
    (void)c;
    g_hw.dac[channel] = raw;
    return OS_STATUS_OK;
}

static void dac_stop(void* c, uint32_t channel)
{
    (void)c;
    g_hw.dac_on[channel] = 0;
}

static OsStatus adc_read(void* c, uint32_t channel, uint32_t* raw)
{
    (void)c;
    *raw = 1000U + channel * 10U + os_get_tick_ms() % 7U;
    return OS_STATUS_OK;
}

static OsStatus counter_start(void* c, uint32_t unit, uint32_t mode, uint32_t filter_ns, uint32_t* out_filter_ns)
{
    (void)c;
    g_hw.counter_mode[unit] = mode;
    g_hw.count[unit] = 0;
    *out_filter_ns = filter_ns;
    return OS_STATUS_OK;
}

static OsStatus counter_read(void* c, uint32_t unit, int32_t* count)
{
    (void)c;
    *count = g_hw.count[unit];
    return OS_STATUS_OK;
}

static OsStatus counter_write(void* c, uint32_t unit, int32_t count)
{
    (void)c;
    g_hw.count[unit] = count;
    return OS_STATUS_OK;
}

static void counter_stop(void* c, uint32_t unit)
{
    (void)c;
    g_hw.counter_mode[unit] = 0;
}

static OsStatus pulse_configure(void* c, uint32_t pin, uint32_t flags)
{
    (void)c;
    g_hw.pulse_flags[pin] = flags | 0x100U;
    return OS_STATUS_OK;
}

static OsStatus pulse_send(void* c, uint32_t pin, const uint32_t* items, uint32_t count, uint32_t carrier_hz)
{
    (void)c, (void)pin, (void)items, (void)count, (void)carrier_hz;
    return OS_STATUS_OK;
}

static void pulse_finish(void* c, uint32_t pin)
{
    (void)c, (void)pin;
}

static void pulse_release(void* c, uint32_t pin)
{
    (void)c;
    g_hw.pulse_flags[pin] = 0;
}

static OsStatus uart_configure(void* c, uint32_t uart, uint32_t baud, uint32_t format, uint32_t* out_baud)
{
    (void)c;
    g_hw.uart_baud[uart] = baud;
    g_hw.uart_format[uart] = format;
    *out_baud = baud;
    return OS_STATUS_OK;
}

static OsStatus uart_write(void* c, uint32_t uart, const uint8_t* data, uint32_t size, uint32_t* written)
{
    (void)c, (void)uart;
    if (g_tx_count + size <= sizeof(g_tx))
    {
        memcpy(g_tx + g_tx_count, data, size);
        g_tx_count += size;
    }
    *written = size;
    return OS_STATUS_OK;
}

static OsStatus i2c_transaction(void* c, uint32_t bus, uint32_t address, uint32_t hz, const OsI2cOp* ops, uint32_t count)
{
    uint8_t reg = 0;
    uint32_t i;
    (void)c, (void)hz;
    for (i = 0; i < count; i++)
    {
        if (ops[i].kind == OS_I2C_WRITE && ops[i].length > 0U)
            reg = ops[i].data[0];
        else if (ops[i].kind == OS_I2C_READ && ops[i].length > 0U)
            ops[i].data[0] = (uint8_t)(address + bus + reg * 3U);
    }
    return OS_STATUS_OK;
}

static OsStatus spi_transaction(void* c, uint32_t bus, uint32_t mode, uint32_t hz, const OsSpiOp* ops, uint32_t count)
{
    uint32_t i;
    uint32_t k;
    (void)c, (void)bus;
    for (i = 0; i < count; i++)
        for (k = 0; ops[i].kind == OS_SPI_TRANSFER && k < ops[i].length; k++)
            ops[i].read[k] = (uint8_t)(ops[i].write[k] ^ mode ^ (hz >> 16));
    return OS_STATUS_OK;
}

static OsStatus onewire_transaction(void* c, uint32_t bus, const OsOneWireOp* ops, uint32_t count)
{
    (void)c, (void)bus, (void)ops, (void)count;
    return OS_STATUS_OK;
}

static OsStatus display_write(void* c, uint32_t display, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                              uint32_t format, const void* pixels, uint32_t size)
{
    (void)c, (void)display, (void)x, (void)y, (void)width, (void)height, (void)format, (void)pixels, (void)size;
    return OS_STATUS_OK;
}

static OsStatus display_brightness(void* c, uint32_t display, uint8_t level)
{
    (void)c;
    g_hw.brightness[display] = level;
    return OS_STATUS_OK;
}

static const OsGpioPort g_gpio_port = { NULL, gpio_configure, gpio_write, gpio_read };
static const OsPwmPort g_pwm_port = { NULL, pwm_start, pwm_duty, pwm_frequency, pwm_stop };
static const OsDacPort g_dac_port = { NULL, dac_start, dac_write, dac_stop };
static const OsAdcPort g_adc_port = { NULL, adc_read, NULL };
static const OsCounterPort g_counter_port = { NULL, counter_start, counter_read, counter_write, NULL, counter_stop };
static const OsPulsePort g_pulse_port = { NULL, pulse_configure, pulse_send, NULL, NULL, pulse_finish, pulse_release, 125U };
static const OsUartPort g_uart_port = { NULL, uart_configure, uart_write, NULL };
static const OsI2cPort g_i2c_port = { NULL, i2c_transaction, NULL };
static const OsSpiPort g_spi_port = { NULL, spi_transaction, NULL };
static const OsOneWirePort g_onewire_port = { NULL, onewire_transaction, NULL, NULL };
static const OsDisplayPort g_display_port = { NULL, display_write, NULL, NULL, display_brightness };

/* Which devices the firmware registers this time. */
static int g_with_counter = 1;
static int g_display_keeps_image = 1;
static int g_reordered;

/* What the port does at every start: the kernel and every device, by name. */
static void boot(void)
{
    OsDacInfo dac = { 10U, 3300U };
    OsAdcInfo adc = { 12U, 3300U };
    OsCounterInfo counter = { OS_COUNTER_PULSE, 0U };
    OsInputInfo keys;
    OsDisplayInfo display;

    memset(&g_hw, 0, sizeof(g_hw));
    memset(&keys, 0, sizeof(keys));
    memset(&display, 0, sizeof(display));
    keys.kind = OS_INPUT_BUTTONS;
    display.width = 64U;
    display.height = 32U;
    display.format = OS_DISPLAY_RGB565;
    display.formats = OS_DISPLAY_FORMAT_BIT(OS_DISPLAY_RGB565);
    display.has_brightness = 1U;
    display.brightness = 200U;
    display.keeps_image = (uint8_t)g_display_keeps_image;

    os_init();
    os_gpio_port_set(&g_gpio_port);
    if (g_reordered)
    {
        /* Another firmware: the same names, registered in another order, so in other slots. */
        os_gpio_add_pin("ir", 7U, OS_GPIO_CAN_OUTPUT | OS_GPIO_CAN_PULSE, 0U);
        os_gpio_add_pin("spare", 3U, OS_GPIO_CAN_OUTPUT, 0U);
        os_gpio_add_pin("cs", 6U, OS_GPIO_CAN_OUTPUT, 0U);
        os_gpio_add_pin("motor", 5U, OS_GPIO_CAN_OUTPUT | OS_GPIO_CAN_PWM, 0U);
        os_gpio_add_pin("led", 2U, OS_GPIO_CAN_INPUT | OS_GPIO_CAN_OUTPUT, 0U);
        os_gpio_add_pin("button", 0U, OS_GPIO_CAN_INPUT | OS_GPIO_CAN_PULL_UP | OS_GPIO_CAN_EDGES, 0U);
    }
    else
    {
        os_gpio_add_pin("button", 0U, OS_GPIO_CAN_INPUT | OS_GPIO_CAN_PULL_UP | OS_GPIO_CAN_EDGES, 0U);
        os_gpio_add_pin("led", 2U, OS_GPIO_CAN_INPUT | OS_GPIO_CAN_OUTPUT, 0U);
        os_gpio_add_pin("motor", 5U, OS_GPIO_CAN_OUTPUT | OS_GPIO_CAN_PWM, 0U);
        os_gpio_add_pin("cs", 6U, OS_GPIO_CAN_OUTPUT, 0U);
        os_gpio_add_pin("ir", 7U, OS_GPIO_CAN_OUTPUT | OS_GPIO_CAN_PULSE, 0U);
    }
    os_pwm_port_set(&g_pwm_port);
    os_dac_port_set(&g_dac_port);
    os_dac_add_channel("dac0", 1U, &dac);
    os_adc_port_set(&g_adc_port);
    os_adc_add_channel("adc0", 1U, &adc);
    os_counter_port_set(&g_counter_port);
    if (g_with_counter)
        os_counter_add("enc", 1U, &counter);
    os_pulse_port_set(&g_pulse_port);
    os_uart_port_set(&g_uart_port);
    os_uart_add("uart1", 1U, 0U);
    os_input_add("keys", 1U, &keys);
    os_i2c_port_set(&g_i2c_port);
    os_i2c_add_bus("i2c0", 0U, 400000U);
    os_spi_port_set(&g_spi_port);
    os_spi_add_bus("spi0", 0U, 8000000U);
    os_onewire_port_set(&g_onewire_port);
    os_onewire_add_bus("ow0", 0U);
    os_display_port_set(&g_display_port);
    os_display_add("main", 1U, &display);
}

#define TASKS 6
static const char* const g_names[TASKS] = {"pins", "analog", "counter", "serial", "keys", "buses"};
static Bin g_module;
static uint32_t g_ids[TASKS];

static void start(void)
{
    static const char* const entries[TASKS] = {"app_main_pins", "app_main_analog", "app_main_counter",
                                               "app_main_serial", "app_main_keys", "app_main_buses"};
    uint32_t i;
    for (i = 0; i < TASKS; i++)
    {
        OsTaskHandle t = NULL;
        os_task_create(&t, g_module.d, g_module.n, entries[i], g_names[i], 0U, OS_TASK_PRIORITY_NORMAL);
        g_ids[i] = t ? os_task_get_id(t) : 0U;
    }
}

static OsStatus find(void* context, const char* name, const uint8_t** out_bytes, uint32_t* out_size)
{
    (void)context, (void)name;
    *out_bytes = g_module.d;
    *out_size = g_module.n;
    return OS_STATUS_OK;
}

/* The world outside, at millisecond ms: a counter counting, the button, bytes, keys. */
static uint32_t g_ms;
/* The press that woke the board, which the port reports after the restore. */
static int g_button_delivered;
/* A press and a key that came just before the board went to sleep, already given to the kernel. */
static int g_early_delivered;

static void world(void)
{
    static const uint8_t first[] = "hello";
    static const uint8_t second[] = "later";
    if (g_with_counter)
        g_hw.count[1] += 1;
    if (g_ms == 4U || g_ms == 23U)
        os_gpio_edge(0U, OS_GPIO_EDGE_FALLING);
    /* Two presses at 10 ms: one just before the sleep, one that wakes the board. */
    if (g_ms == 10U && !g_early_delivered)
        os_gpio_edge(0U, OS_GPIO_EDGE_FALLING);
    if (g_ms == 10U && !g_button_delivered)
        os_gpio_edge(0U, OS_GPIO_EDGE_FALLING);
    if (g_ms == 10U && !g_early_delivered)
        os_input_key(1U, 31U, 1U);
    if (g_ms == 7U)
        os_uart_receive(1U, first, 5U);
    if (g_ms == 17U)
        os_uart_receive(1U, second, 5U);
    if (g_ms == 6U || g_ms == 21U)
        os_input_key(1U, 30U, 1U);
    if (g_ms == 8U || g_ms == 22U)
        os_input_key(1U, 30U, 0U);
}

/* One millisecond: the world, then every task that is ready runs until none is. */
static void step(void)
{
    uint32_t guard;
    ++g_ms;
    world();
    os_tick(1U);
    for (guard = 0; guard < 1000U && os_schedule() != OS_STATUS_NO_READY_TASKS; guard++)
    {
    }
}

static int all_dead(void)
{
    uint32_t i;
    for (i = 0; i < TASKS; i++)
    {
        OsTaskHandle t = os_task_find_by_id(g_ids[i]);
        if (t && os_task_get_state(t) != OS_TASK_DEAD)
            return 0;
    }
    return 1;
}

static int32_t result_of(uint32_t id)
{
    OsTaskHandle t = os_task_find_by_id(id);
    OsValue v;
    if (!t || os_task_get_state(t) != OS_TASK_DEAD || os_task_get_return_value(t, &v) != OS_STATUS_OK)
        return -100000;
    return (int32_t)v.value.i32;
}

/* A stream in host memory, standing in for flash or an SD card. */
typedef struct
{
    uint8_t* d;
    uint32_t n;
    uint32_t cap;
    uint32_t at;
} Stream;

static OsStatus stream_write(void* context, const void* data, uint32_t size)
{
    Stream* s = (Stream*)context;
    if (s->n + size > s->cap)
    {
        uint32_t cap = (s->n + size) * 2U;
        uint8_t* d = realloc(s->d, cap);
        if (!d)
            return OS_STATUS_OUT_OF_MEMORY;
        s->d = d;
        s->cap = cap;
    }
    memcpy(s->d + s->n, data, size);
    s->n += size;
    return OS_STATUS_OK;
}

static OsStatus stream_read(void* context, void* data, uint32_t size)
{
    Stream* s = (Stream*)context;
    if (size > s->n - s->at)
        return OS_STATUS_INVALID_ARGUMENT;
    memcpy(data, s->d + s->at, size);
    s->at += size;
    return OS_STATUS_OK;
}

static int32_t g_expected[TASKS];
static uint8_t g_expected_tx[256];
static uint32_t g_expected_tx_count;

static void run_to_end(void)
{
    uint32_t i;
    for (i = 0; i < 2000U && !all_dead(); i++)
        step();
}

int main(void)
{
    Stream s = {0};
    Hardware before;
    OsGpioWake wake[4];
    uint32_t i;

    l = fopen("system_devices.log", "w");
    ok(load(&g_module), "load system_devices.wasm");
    if (!g_module.d)
        return 1;

    /* The run that is never stopped. */
    boot();
    start();
    run_to_end();
    {
        int every = 1;
        for (i = 0; i < TASKS; i++)
        {
            g_expected[i] = result_of(g_ids[i]);
            every &= g_expected[i] != -100000;
        }
        ok(every, "uninterrupted: every program ends");
    }
    memcpy(g_expected_tx, g_tx, g_tx_count);
    g_expected_tx_count = g_tx_count;
    os_shutdown();

    /* Saved after 9 ms, the hardware cleared, restored; the button wakes the board. */
    g_ms = 0;
    g_tx_count = 0;
    boot();
    start();
    for (i = 0; i < 9U; i++)
        step();
    ok(os_gpio_wake_pins(wake, 4U) == 1U && wake[0].pin == 0U && wake[0].edges == OS_GPIO_EDGE_FALLING,
       "the button the program waits on is the one wake pin, its falling edge");
    /* A press and a key just before the sleep: the kernel has them, no program has read them. */
    os_gpio_edge(0U, OS_GPIO_EDGE_FALLING);
    os_input_key(1U, 31U, 1U);
    g_early_delivered = 1;
    ok(g_hw.gpio_level[2] == 0U && (g_hw.gpio_flags[2] & OS_GPIO_INIT_HIGH) != 0U,
       "the LED was opened high and the program has set it low");
    before = g_hw;
    s.n = 0;
    ok(os_system_save(stream_write, &s) == OS_STATUS_OK, "save the system while every device is held");
    printf("  stream: %u bytes\n", s.n);
    os_shutdown();
    g_reordered = 1;
    boot();
    g_reordered = 0;
    ok(g_hw.gpio_flags[2] == 0U && g_hw.pwm_on[5] == 0U, "after the sleep the hardware is as at power-on");
    s.at = 0;
    ok(os_system_restore(stream_read, &s, find, NULL, 0U) == OS_STATUS_OK,
       "restore into a firmware that registers the pins in another order");
    /* A pin's state is its mode and level; how it was told its first level does not matter. */
    for (i = 0; i < 8U; i++)
    {
        before.gpio_flags[i] &= ~OS_GPIO_INIT_HIGH;
        g_hw.gpio_flags[i] &= ~OS_GPIO_INIT_HIGH;
    }
    ok(memcmp(&before, &g_hw, sizeof(g_hw)) == 0,
       "every device is set up as it was: pins, levels, PWM, DAC, counter, pulse, serial port, brightness");
    ok(os_spi_get_owner(0U, 6U) == g_ids[5] && os_gpio_get_owner(6U) == g_ids[5],
       "the SPI device and its chip select pin are the program's again");
    /* The press that woke the board, as the port reports it. */
    os_gpio_edge(0U, OS_GPIO_EDGE_FALLING);
    g_button_delivered = 1;
    run_to_end();
    g_button_delivered = 0;
    g_early_delivered = 0;
    {
        int same = 1;
        for (i = 0; i < TASKS; i++)
        {
            if (result_of(g_ids[i]) != g_expected[i])
            {
                printf("  %s: %d, expected %d\n", g_names[i], result_of(g_ids[i]), g_expected[i]);
                same = 0;
            }
        }
        ok(same, "every program ends with the result of the run never stopped");
    }
    ok(g_tx_count == g_expected_tx_count && memcmp(g_tx, g_expected_tx, g_tx_count) == 0,
       "the serial line carried the same bytes");
    os_shutdown();

    /* After the sleep the firmware has no counter and the PWM port refuses: those programs end, the rest go on. */
    g_ms = 0;
    g_tx_count = 0;
    boot();
    start();
    for (i = 0; i < 9U; i++)
        step();
    os_gpio_edge(0U, OS_GPIO_EDGE_FALLING);
    os_input_key(1U, 31U, 1U);
    g_early_delivered = 1;
    s.n = 0;
    os_system_save(stream_write, &s);
    os_shutdown();
    g_with_counter = 0;
    g_pwm_broken = 1;
    boot();
    s.at = 0;
    ok(os_system_restore(stream_read, &s, find, NULL, 0U) == OS_STATUS_OK, "restore with a device gone and one broken");
    {
        OsTaskHandle counter = os_task_find_by_id(g_ids[2]);
        OsTaskHandle analog = os_task_find_by_id(g_ids[1]);
        const char* error = os_task_get_error(counter);
        ok(os_task_get_state(counter) == OS_TASK_DEAD && error &&
               strcmp(error, "restore: a device it held is gone") == 0,
           "the program whose counter is gone ended: restore: a device it held is gone");
        error = os_task_get_error(analog);
        ok(os_task_get_state(analog) == OS_TASK_DEAD && error &&
               strcmp(error, "restore: a device it held cannot be set up") == 0,
           "the program whose PWM output cannot start ended: restore: a device it held cannot be set up");
        ok(os_gpio_get_owner(5U) == 0U && os_dac_get_owner(1U) == 0U,
           "and its pin and analog output are free again");
    }
    os_gpio_edge(0U, OS_GPIO_EDGE_FALLING);
    g_button_delivered = 1;
    run_to_end();
    g_button_delivered = 0;
    g_early_delivered = 0;
    ok(result_of(g_ids[0]) == g_expected[0] && result_of(g_ids[3]) == g_expected[3] &&
           result_of(g_ids[4]) == g_expected[4] && result_of(g_ids[5]) == g_expected[5],
       "every other program ends with the result of the run never stopped");
    os_shutdown();
    g_with_counter = 1;
    g_pwm_broken = 0;

    /* A display that loses its picture in the sleep: saving waits until no program holds it. */
    g_ms = 0;
    g_display_keeps_image = 0;
    boot();
    start();
    for (i = 0; i < 9U; i++)
        step();
    s.n = 0;
    ok(os_system_save(stream_write, &s) == OS_STATUS_UNSUPPORTED &&
           strstr(os_get_last_error_result(), "display") != NULL,
       "a display that loses its picture blocks saving, and the error says so");
    os_shutdown();
    g_display_keeps_image = 1;

    free(s.d);
    free(g_module.d);
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
