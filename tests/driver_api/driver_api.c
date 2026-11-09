#include <wasm-rtos.h>

/* A driver the test's host adds: wasm-rtos:kernel/echo. */
#define ECHO(name) __attribute__((import_module("wasm-rtos:kernel/echo@1.0.0"), import_name(name)))
ECHO("twice") int32_t echo_twice(int32_t value);
ECHO("open") int32_t echo_open(void);
ECHO("id") int32_t echo_id(int32_t handle);

/* No platform pins and no ADC channels: their interfaces answer unsupported. */
int app_main_absent(void)
{
    int32_t gpio = wr_gpio_open("led", 3U, 0U);
    int32_t adc = wr_adc_open("a0", 2U, 0U);
    return gpio == -WR_EUNSUPPORTED && adc == -WR_EUNSUPPORTED ? 1 : gpio * 100 + adc;
}

/* A driver of the host's own: calls, a handle, closing it. */
int app_main_custom(void)
{
    int32_t handle = echo_open();
    if (handle <= 0)
        return -100;
    if (echo_id(handle) != 7)
        return -101;
    if (wr_close(handle) != 0)
        return -102;
    if (echo_id(handle) != -WR_EBADH)
        return -103;
    return echo_twice(21);
}
