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

#define WASM_FILE "pwm_api.wasm"

static int load(Bin* b)
{
    FILE* x = fopen(WASM_FILE, "rb");
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

/* PWM hardware with two channels: what each pin runs at. */
typedef struct
{
    uint8_t running;
    uint32_t frequency;
    uint32_t duty;
} FakePwm;

static FakePwm g_pwm[8];
static uint32_t g_gpio_flags[8];

static OsStatus gpio_configure(void* context, uint32_t pin, uint32_t flags)
{
    (void)context;
    g_gpio_flags[pin] = flags;
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

static uint32_t running_count(void)
{
    uint32_t count = 0;
    for (uint32_t pin = 0; pin < 8U; pin++)
        count += g_pwm[pin].running;
    return count;
}

/* Frequencies are multiples of 10 Hz on this hardware. */
static OsStatus pwm_start(void* context, uint32_t pin, uint32_t frequency_hz, uint32_t duty, uint32_t* out_frequency_hz)
{
    (void)context;
    if (running_count() >= 2U)
        return OS_STATUS_LIMIT_REACHED;
    g_pwm[pin].running = 1U;
    g_pwm[pin].frequency = frequency_hz / 10U * 10U;
    g_pwm[pin].duty = duty;
    *out_frequency_hz = g_pwm[pin].frequency;
    return OS_STATUS_OK;
}

static OsStatus pwm_set_duty(void* context, uint32_t pin, uint32_t duty)
{
    (void)context;
    g_pwm[pin].duty = duty;
    return OS_STATUS_OK;
}

static OsStatus pwm_set_frequency(void* context, uint32_t pin, uint32_t frequency_hz, uint32_t* out_frequency_hz)
{
    (void)context;
    g_pwm[pin].frequency = frequency_hz / 10U * 10U;
    *out_frequency_hz = g_pwm[pin].frequency;
    return OS_STATUS_OK;
}

static void pwm_stop(void* context, uint32_t pin)
{
    (void)context;
    memset(&g_pwm[pin], 0, sizeof(g_pwm[pin]));
}

static OsTaskHandle start(Bin* b, const char* entry)
{
    OsTaskHandle t = NULL;
    os_task_create(&t, b->d, b->n, entry, entry, 0U, OS_TASK_PRIORITY_NORMAL);
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
    for (uint32_t i = 0; t && i < 1000U && os_task_get_state(t) != OS_TASK_DEAD; i++)
        if (os_schedule() == OS_STATUS_NO_READY_TASKS)
            os_tick(10U);
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
    OsGpioPort gpio = { NULL, gpio_configure, gpio_write, gpio_read };
    OsPwmPort pwm = { NULL, pwm_start, pwm_set_duty, pwm_set_frequency, pwm_stop };
    OsPwmPort broken = { NULL, pwm_start, NULL, NULL, pwm_stop };
    OsTaskHandle t = NULL;

    l = fopen("pwm_api.log", "w");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load pwm_api.wasm");
    if (!b.d)
        return 1;
    ok(os_gpio_port_set(&gpio) == OS_STATUS_OK &&
           os_gpio_add_pin("led", 2U, OS_GPIO_CAN_OUTPUT | OS_GPIO_CAN_PWM, OS_GPIO_ACTIVE_LOW) == OS_STATUS_OK &&
           os_gpio_add_pin("motor", 4U, OS_GPIO_CAN_OUTPUT | OS_GPIO_CAN_PWM, 0U) == OS_STATUS_OK &&
           os_gpio_add_pin("gpio7", 7U, OS_GPIO_CAN_ALL, 0U) == OS_STATUS_OK &&
           os_gpio_add_pin("button", 0U, OS_GPIO_CAN_INPUT, 0U) == OS_STATUS_OK,
       "pins: an active-low LED and a motor driver that can do PWM, a button that cannot");
    ok(finish(start(&b, "app_main_try"), "app_main_try") == -13, "without a port pwm-open returns -WR_EUNSUPPORTED");
    ok(os_pwm_port_set(&broken) == OS_STATUS_INVALID_ARGUMENT && os_pwm_port_set(&pwm) == OS_STATUS_OK,
       "a port needs start, set_duty and stop");

    t = start(&b, "app_main_led");
    run_until_waiting(t);
    ok(g_pwm[2].running && g_pwm[2].frequency == 1000U, "the LED runs at 1 kHz");
    ok(g_pwm[2].duty == 65535U - 16384U, "a quarter on, on an active-low pin, is three quarters high on the pin");
    ok(g_gpio_flags[2] & OS_GPIO_OUTPUT, "the pin is owned as an output");
    ok(finish(t, "app_main_led") == 0, "duty, frequency rounding, ownership");
    ok(!g_pwm[2].running && g_gpio_flags[2] == 0U, "the output stops and the pin is an input again when the program ends");

    t = start(&b, "app_main_motor");
    run_until_waiting(t);
    ok(g_pwm[4].running && g_pwm[4].duty == 0U, "fully on with the program's active-low flag is low on the pin");
    finish(t, "app_main_motor");
    ok(finish(start(&b, "app_main_errors"), "app_main_errors") == 0,
       "pins without PWM, unknown names, bad frequency and flags, no free channel, not waitable");
    ok(running_count() == 0U, "every output stopped");

    free(b.d);
    ok(os_get_task_count() == 0U, "clean OS state");
    os_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
