#include <wasm-rtos.h>

/* Built for ABI 1.0: kernels of 1.1 still link it. */
__attribute__((import_module("wasm-rtos:kernel/sync@1.0.0"), import_name("event-create")))
int32_t old_event_create(const char* name, uint32_t name_length);

/* The board LED is wired active-low; the program still writes 1 for "on". */
int32_t app_main_led(void)
{
    int32_t led = wr_gpio_open_cstr("led", WR_GPIO_OUTPUT | WR_GPIO_INIT_HIGH);

    if (led <= 0)
        return 1;
    if (wr_gpio_get(led) != 1)
        return 2;
    /* Another name of the same pin is the same pin. */
    if (wr_gpio_open_cstr("gpio2", 0U) != -WR_EINUSE)
        return 3;
    wr_sleep_ms(10U);
    if (wr_gpio_set(led, 0U) != 0 || wr_gpio_get(led) != 0)
        return 4;
    wr_sleep_ms(10U);
    if (wr_gpio_set(led, 2U) != -WR_EINVAL)
        return 5;
    /* Ends with the pin open: the kernel releases it. */
    return 0;
}

int32_t app_main_errors(void)
{
    int32_t sensor;

    if (wr_gpio_open_cstr("nope", 0U) != -WR_ENOTFOUND)
        return 1;
    if (wr_gpio_open("led", 0U, 0U) != -WR_EINVAL)
        return 2;
    if (wr_gpio_open_cstr("sensor", WR_GPIO_PULL_DOWN) != -WR_EUNSUPPORTED)
        return 3;
    if (wr_gpio_open_cstr("sensor", WR_GPIO_PULL_UP | WR_GPIO_PULL_DOWN) != -WR_EINVAL)
        return 4;
    if (wr_gpio_open_cstr("sensor", WR_GPIO_OPEN_DRAIN) != -WR_EINVAL)
        return 5;
    if (wr_gpio_open_cstr("sensor", 0x100U) != -WR_EINVAL)
        return 6;
    sensor = wr_gpio_open_cstr("sensor", WR_GPIO_OUTPUT | WR_GPIO_OPEN_DRAIN | WR_GPIO_PULL_UP);
    if (sensor <= 0)
        return 7;
    if (wr_gpio_watch(sensor, WR_GPIO_RISING) != -WR_EUNSUPPORTED)
        return 8;
    /* A one-wire style pin turns around to listen. */
    if (wr_gpio_set_mode(sensor, WR_GPIO_PULL_UP) != 0 || wr_gpio_set(sensor, 1U) != -WR_EINVAL)
        return 9;
    if (wr_gpio_set(99, 1U) != -WR_EBADH || wr_gpio_events(sensor + 1) != -WR_EBADH)
        return 10;
    if (wr_close(sensor) != 0 || wr_gpio_get(sensor) != -WR_EBADH)
        return 11;
    if (old_event_create(0, 0U) <= 0)
        return 12;
    return 0;
}

/* Waits for presses of an active-low button next to a timer. */
int32_t app_main_button(void)
{
    int32_t handles[2];
    int32_t index;
    int32_t presses;

    handles[0] = wr_gpio_open_cstr("button-a", WR_GPIO_PULL_UP | WR_GPIO_ACTIVE_LOW);
    handles[1] = wr_timer_create(1000U, 0U);
    if (handles[0] <= 0 || handles[1] <= 0)
        return -1;
    if (wr_gpio_watch(handles[0], WR_GPIO_RISING) != 0 || wr_timer_start(handles[1]) != 0)
        return -2;
    index = wr_wait_any(handles, 2U, WR_WAIT_FOREVER);
    if (index != 0)
        return -3;
    presses = wr_gpio_events(handles[0]);
    if (wr_gpio_events(handles[0]) != 0)
        return -4;
    return presses * 10 + wr_gpio_get(handles[0]);
}

/* Holds the LED until it is stopped. */
int32_t app_main_hold(void)
{
    int32_t led = wr_gpio_open_cstr("led", WR_GPIO_OUTPUT | WR_GPIO_INIT_HIGH);
    if (led <= 0)
        return led;
    wr_sleep_ms(100000U);
    return 0;
}

int32_t app_main_try(void)
{
    return wr_gpio_open_cstr("led", WR_GPIO_OUTPUT);
}

/* Takes the LED, turns it on and traps. */
void app_main_crash(void)
{
    wr_gpio_open_cstr("led", WR_GPIO_OUTPUT | WR_GPIO_INIT_HIGH);
    __builtin_trap();
}
