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
    FILE* x = fopen("arduino_analog.wasm", "rb");
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

/* The board: a 12-bit potentiometer on "a0" at 3/4, the LED on pin 2 and a buzzer on pin 5 with PWM, "uart1". */
static OsStatus adc_read(void* context, uint32_t channel, uint32_t* out_raw)
{
    (void)context;
    (void)channel;
    *out_raw = 3071U;
    return OS_STATUS_OK;
}

static OsStatus gpio_configure(void* context, uint32_t pin, uint32_t flags)
{
    (void)context;
    (void)pin;
    (void)flags;
    return OS_STATUS_OK;
}

static uint8_t g_led_level = 9;

static OsStatus gpio_write(void* context, uint32_t pin, uint8_t level)
{
    (void)context;
    if (pin == 2U)
        g_led_level = level;
    return OS_STATUS_OK;
}

static OsStatus gpio_read(void* context, uint32_t pin, uint8_t* out_level)
{
    (void)context;
    (void)pin;
    *out_level = 0U;
    return OS_STATUS_OK;
}

/* What the LED and the buzzer did, in order. */
static char g_events[512];

static void note(const char* format, uint32_t pin, uint32_t value)
{
    size_t used = strlen(g_events);
    snprintf(g_events + used, sizeof(g_events) - used, format, (unsigned)pin, (unsigned)value);
}

static OsStatus pwm_start(void* context, uint32_t pin, uint32_t frequency_hz, uint32_t duty, uint32_t* out_frequency_hz)
{
    (void)context;
    (void)duty;
    note("start %u@%u ", pin, frequency_hz);
    *out_frequency_hz = frequency_hz;
    return OS_STATUS_OK;
}

static OsStatus pwm_set_duty(void* context, uint32_t pin, uint32_t duty)
{
    (void)context;
    note("duty %u=%u ", pin, duty);
    return OS_STATUS_OK;
}

static OsStatus pwm_set_frequency(void* context, uint32_t pin, uint32_t frequency_hz, uint32_t* out_frequency_hz)
{
    (void)context;
    note("freq %u=%u ", pin, frequency_hz);
    *out_frequency_hz = frequency_hz;
    return OS_STATUS_OK;
}

static void pwm_stop(void* context, uint32_t pin)
{
    (void)context;
    note("stop %u ", pin, 0U);
}

static uint32_t g_uart_format;
static char g_uart_sent[32];

static OsStatus uart_configure(void* context, uint32_t uart, uint32_t baud, uint32_t format, uint32_t* out_baud)
{
    (void)context;
    (void)uart;
    g_uart_format = format;
    *out_baud = baud;
    return OS_STATUS_OK;
}

/* The peer answers a whole line at once. */
static OsStatus uart_write(void* context, uint32_t uart, const uint8_t* data, uint32_t size, uint32_t* out_written)
{
    static const uint8_t pong[] = "pong\n";
    (void)context;
    strncat(g_uart_sent, (const char*)data, size < 16U ? size : 16U);
    *out_written = size;
    if (memchr(data, '\n', size) != NULL)
        os_uart_receive(uart, pong, sizeof(pong) - 1U);
    return OS_STATUS_OK;
}

static char g_output[256];

static void capture(void* context, uint32_t task_id, uint32_t fd, const void* data, uint32_t size)
{
    size_t used = strlen(g_output);
    (void)context;
    (void)task_id;
    (void)fd;
    if (used + size < sizeof(g_output))
        memcpy(g_output + used, data, size);
}

int main(void)
{
    Bin b = {0};
    OsAdcPort adc = { NULL, adc_read, NULL };
    OsAdcInfo pot = { 12U, 3300U };
    OsGpioPort gpio = { NULL, gpio_configure, gpio_write, gpio_read };
    OsPwmPort pwm = { NULL, pwm_start, pwm_set_duty, pwm_set_frequency, pwm_stop };
    OsUartPort uart = { NULL, uart_configure, uart_write, NULL };
    OsOutputPort output = { NULL, capture };
    OsTaskHandle t = NULL;
    uint32_t i;

    l = fopen("arduino_analog.log", "w");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load arduino_analog.wasm");
    if (!b.d)
        return 1;
    ok(os_adc_port_set(&adc) == OS_STATUS_OK && os_adc_add_channel("a0", 0U, &pot) == OS_STATUS_OK &&
           os_gpio_port_set(&gpio) == OS_STATUS_OK && os_gpio_add_pin("led", 2U, OS_GPIO_CAN_ALL, 0U) == OS_STATUS_OK &&
           os_gpio_add_pin("gpio5", 5U, OS_GPIO_CAN_ALL, 0U) == OS_STATUS_OK && os_pwm_port_set(&pwm) == OS_STATUS_OK &&
           os_uart_port_set(&uart) == OS_STATUS_OK && os_uart_add("uart1", 1U, 0U) == OS_STATUS_OK &&
           os_output_port_set(&output) == OS_STATUS_OK,
       "a board with a potentiometer, PWM pins and a serial port");
    ok(os_task_create(&t, b.d, b.n, "_start", "sketch", 65536U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK, "start the sketch");
    for (i = 0; i < 100000U && os_task_get_state(t) != OS_TASK_DEAD; i++)
        if (os_schedule() == OS_STATUS_NO_READY_TASKS)
            os_tick(1U);
    ok(os_task_get_state(t) == OS_TASK_DEAD && os_task_get_exit_code(t) == 0U, "the sketch got its reply and exited with 0");
    ok(strcmp(g_output, "767\r\n3071\r\n3071\r\nreply pong\r\n") == 0,
       "analogRead: 3071 of 4095 is 767 of 1023, then 3071 at 12 bits; pin 0 is A0");
    if (strcmp(g_output, "767\r\n3071\r\n3071\r\nreply pong\r\n") != 0)
        printf("got: %s\n", g_output);
    printf("     pwm: %s\n", g_events);
    ok(strstr(g_events, "start 2@1000 duty 2=16448 duty 2=65535 stop 2 ") == g_events,
       "analogWrite: 64 of 255 then 4095 of 4095 at 1 kHz; digitalWrite stops the PWM");
    ok(g_led_level == 0U, "and drives the pin low");
    ok(strstr(g_events, "start 5@440 freq 5=440 duty 5=32768 duty 5=0 ") != NULL, "tone: 440 Hz at half duty, off after its duration");
    ok(g_uart_format == OS_UART_PARITY_EVEN && strcmp(g_uart_sent, "ping\n") == 0, "Serial1: SERIAL_8E1, the line sent");

    os_task_delete(t);
    free(b.d);
    ok(os_get_task_count() == 0U, "clean OS state");
    os_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
