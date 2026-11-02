#include <wasm-rtos.h>

int32_t app_main_try(void)
{
    return wr_pwm_open_cstr("led", 1000u, 0u);
}

/* The board's LED is active-low: the program still means "on" by a large duty. */
int32_t app_main_led(void)
{
    int32_t led = wr_pwm_open_cstr("led", 1000u, 0u);

    if (led <= 0)
        return 1;
    if (wr_pwm_get(led, WR_PWM_FREQUENCY) != 1000 || wr_pwm_get(led, WR_PWM_DUTY) != 0)
        return 2;
    if (wr_pwm_set_duty(led, 16384u) != 0 || wr_pwm_get(led, WR_PWM_DUTY) != 16384)
        return 3;
    wr_sleep_ms(10u);
    if (wr_pwm_set_duty(led, 65536u) != -WR_EINVAL)
        return 4;
    /* The hardware rounds 333 Hz. */
    if (wr_pwm_set_frequency(led, 333u) != 0 || wr_pwm_get(led, WR_PWM_FREQUENCY) != 330)
        return 5;
    if (wr_gpio_open_cstr("led", WR_GPIO_OUTPUT) != -WR_EINUSE)
        return 6;
    return 0;
}

/* A pin flagged active-low by the program, on a pin the board does not invert. */
int32_t app_main_motor(void)
{
    int32_t motor = wr_pwm_open_cstr("motor", 20000u, WR_PWM_ACTIVE_LOW);
    if (motor <= 0)
        return 1;
    if (wr_pwm_set_duty(motor, 65535u) != 0)
        return 2;
    wr_sleep_ms(10u);
    return 0;
}

int32_t app_main_errors(void)
{
    int32_t first;
    if (wr_pwm_open_cstr("button", 1000u, 0u) != -WR_EUNSUPPORTED)
        return 1;
    if (wr_pwm_open_cstr("nope", 1000u, 0u) != -WR_ENOTFOUND || wr_pwm_open_cstr("led", 0u, 0u) != -WR_EINVAL ||
        wr_pwm_open_cstr("led", 1000u, 4u) != -WR_EINVAL)
        return 2;
    first = wr_pwm_open_cstr("led", 1000u, 0u);
    if (first <= 0)
        return 3;
    /* The port has one channel left after "led": "motor" takes it, "gpio7" finds none. */
    if (wr_pwm_open_cstr("motor", 1000u, 0u) <= 0 || wr_pwm_open_cstr("gpio7", 1000u, 0u) != -WR_ELIMIT)
        return 4;
    if (wr_wait_any(&first, 1u, 0u) != -WR_EBADH || wr_pwm_set_duty(99, 1u) != -WR_EBADH)
        return 5;
    return 0;
}
