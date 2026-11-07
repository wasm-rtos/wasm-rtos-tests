#include <wasm-rtos.h>

int32_t app_main_try(void)
{
    return wr_dac_open_cstr("gpio25");
}

/* Properties, raw values and voltages, limits, ownership of the output and of its pin. */
int32_t app_main_basic(void)
{
    int32_t out = wr_dac_open_cstr("gpio25");
    int32_t chip = 0;

    if (out <= 0)
        return 1;
    if (wr_dac_get(out, WR_DAC_RESOLUTION) != 8 || wr_dac_get(out, WR_DAC_MAX) != 255 ||
        wr_dac_get(out, WR_DAC_FULL_SCALE_MV) != 3300 || wr_dac_get(out, WR_DAC_VALUE) != 0 ||
        wr_dac_get(out, 9u) != -WR_EUNSUPPORTED)
        return 2;
    if (wr_dac_write(out, 128u) != 0 || wr_dac_get(out, WR_DAC_VALUE) != 128 || wr_dac_write(out, 256u) != -WR_EINVAL)
        return 3;
    /* 1650 mV of 3300 is half of 255 steps: 127.5 rounds to 128. */
    if (wr_dac_write_mv(out, 1650u) != 0 || wr_dac_get(out, WR_DAC_VALUE) != 128 ||
        wr_dac_write_mv(out, 3301u) != -WR_EINVAL || wr_dac_write_mv(out, 0u) != 0 || wr_dac_get(out, WR_DAC_VALUE) != 0)
        return 4;
    if (wr_dac_open_cstr("gpio25") != -WR_EINUSE || wr_gpio_open_cstr("gpio25", WR_GPIO_OUTPUT) != -WR_EINUSE)
        return 5;
    chip = wr_dac_open_cstr("dac0");
    if (chip <= 0 || wr_dac_write_mv(chip, 2500u) != 0 || wr_dac_get(chip, WR_DAC_VALUE) != 4095 ||
        wr_dac_write_mv(chip, 1250u) != 0 || wr_dac_get(chip, WR_DAC_VALUE) != 2048)
        return 6;
    if (wr_dac_open("gpio26", 6u, 1u) != -WR_EINVAL || wr_dac_open_cstr("gpio26") != -WR_ENOTFOUND ||
        wr_dac_write(-4, 1u) != -WR_EBADH || wr_wait_any(&out, 1u, 0u) != -WR_EBADH)
        return 7;
    return 0;
}

/* Holds the output at 200 a while, for the test to look. */
int32_t app_main_hold(void)
{
    int32_t out = wr_dac_open_cstr("gpio25");
    if (out <= 0 || wr_dac_write(out, 200u) != 0)
        return -1;
    wr_sleep_ms(20u);
    return 0;
}

/* The pin is taken as a GPIO: the output cannot be had meanwhile. */
int32_t app_main_gpio_first(void)
{
    int32_t pin = wr_gpio_open_cstr("gpio25", WR_GPIO_OUTPUT);
    if (pin <= 0)
        return -1;
    return wr_dac_open_cstr("gpio25");
}
