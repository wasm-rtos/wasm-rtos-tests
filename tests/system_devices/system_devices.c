#include <wasm-rtos.h>

/*
 * Programs that hold every kind of device and use them while they sleep
 * and wait: each returns a hash of what it read and when, so a run that was
 * saved and restored ends with the same numbers only if every device came
 * back as it was.
 */

static uint32_t mix(uint32_t hash, uint32_t value)
{
    return (hash ^ value) * 16777619u;
}

static uint32_t now(void)
{
    return (uint32_t)wr_time_ms();
}

/* Sets the LED (opened high, then low at times) and counts presses of the button. */
uint32_t app_main_pins(void)
{
    int32_t led = wr_gpio_open("led", 3, WR_GPIO_OUTPUT | WR_GPIO_INIT_HIGH);
    int32_t button = wr_gpio_open("button", 6, WR_GPIO_PULL_UP);
    uint32_t hash = mix(mix(2166136261u, (uint32_t)(led > 0)), (uint32_t)(button > 0));
    uint32_t i;
    wr_gpio_watch(button, WR_GPIO_FALLING);
    for (i = 0; i < 8; ++i)
    {
        wr_gpio_set(led, i % 3u == 2u);
        hash = mix(hash, (uint32_t)wr_wait_any(&button, 1, 6));
        hash = mix(mix(hash, (uint32_t)wr_gpio_events(button)), now());
    }
    return hash;
}

/* Drives a PWM output and an analog output, reads an analog input. */
uint32_t app_main_analog(void)
{
    int32_t pwm = wr_pwm_open("motor", 5, 1000, 0);
    int32_t dac = wr_dac_open("dac0", 4, 0);
    int32_t adc = wr_adc_open("adc0", 4, 0);
    uint32_t hash = mix(mix(mix(2166136261u, (uint32_t)(pwm > 0)), (uint32_t)(dac > 0)), (uint32_t)(adc > 0));
    uint32_t i;
    wr_pwm_set_duty(pwm, 250);
    wr_dac_write(dac, 100);
    for (i = 0; i < 6; ++i)
    {
        wr_sleep_ms(4);
        hash = mix(hash, (uint32_t)wr_adc_read(adc));
        wr_dac_write(dac, 100 + i);
        wr_pwm_set_duty(pwm, 250 + i);
        if (i == 3)
            wr_pwm_set_frequency(pwm, 2000);
        hash = mix(mix(mix(hash, (uint32_t)wr_pwm_get(pwm, WR_PWM_FREQUENCY)), (uint32_t)wr_pwm_get(pwm, WR_PWM_DUTY)),
                   now());
    }
    return hash;
}

/* Waits for a counter to leave its window and reads it. */
uint32_t app_main_counter(void)
{
    int32_t counter = wr_counter_open("enc", 3, WR_COUNTER_PULSE, 0);
    uint32_t hash = mix(2166136261u, (uint32_t)(counter > 0));
    uint32_t i;
    for (i = 0; i < 4; ++i)
    {
        int32_t value = 0;
        wr_counter_watch(counter, -1000, (int32_t)(i * 6u + 5u));
        hash = mix(hash, (uint32_t)wr_wait_any(&counter, 1, 40));
        wr_counter_read(counter, &value);
        hash = mix(mix(hash, (uint32_t)value), now());
    }
    return hash;
}

/* Reads what arrives on a serial port and answers. */
uint32_t app_main_serial(void)
{
    int32_t port = wr_uart_open("uart1", 5, 115200, WR_UART_PARITY_EVEN | WR_UART_STOP_2);
    uint32_t hash = mix(2166136261u, (uint32_t)(port > 0));
    uint32_t i;
    for (i = 0; i < 5; ++i)
    {
        uint8_t bytes[8];
        int32_t got;
        int32_t k;
        wr_sleep_ms(5);
        got = wr_uart_read(port, bytes, sizeof(bytes));
        hash = mix(hash, (uint32_t)got);
        for (k = 0; k < got; ++k)
            hash = mix(hash, bytes[k]);
        wr_uart_write(port, "ok", 2);
        hash = mix(hash, now());
    }
    return hash;
}

/* Reads key events. */
uint32_t app_main_keys(void)
{
    int32_t keys = wr_input_open("keys", 4, 0);
    uint32_t hash = mix(2166136261u, (uint32_t)(keys > 0));
    uint32_t i;
    for (i = 0; i < 4; ++i)
    {
        wr_input_event events[4];
        int32_t got;
        int32_t k;
        hash = mix(hash, (uint32_t)wr_wait_any(&keys, 1, 12));
        got = wr_input_read(keys, events, 4);
        hash = mix(hash, (uint32_t)got);
        for (k = 0; k < got; ++k)
            hash = mix(mix(mix(mix(hash, events[k].type), events[k].code), (uint32_t)events[k].value),
                       events[k].time_ms);
        hash = mix(hash, now());
    }
    return hash;
}

/* Talks over I2C and SPI, holds a 1-Wire bus, a display and a pulse channel. */
uint32_t app_main_buses(void)
{
    int32_t i2c = wr_i2c_open("i2c0", 4, 0x48, 0);
    int32_t spi = wr_spi_open("spi0", 4, "cs", 2, WR_SPI_MODE_3, 1000000);
    int32_t wire = wr_onewire_open("ow0", 3, 0, 0);
    int32_t display = wr_display_open("main", 4, 0);
    int32_t ir = wr_pulse_open("ir", 2, 0);
    uint32_t hash = 2166136261u;
    uint32_t i;
    hash = mix(mix(mix(mix(mix(hash, (uint32_t)(i2c > 0)), (uint32_t)(spi > 0)), (uint32_t)(wire > 0)),
                   (uint32_t)(display > 0)), (uint32_t)(ir > 0));
    wr_display_set_brightness(display, 77);
    for (i = 0; i < 5; ++i)
    {
        uint8_t reg = (uint8_t)i;
        uint8_t value = 0;
        uint8_t out[2] = {(uint8_t)i, 0xA0u};
        uint8_t in[2] = {0, 0};
        wr_sleep_ms(3);
        hash = mix(mix(hash, (uint32_t)wr_i2c_write_read(i2c, &reg, 1, &value, 1)), value);
        hash = mix(mix(mix(hash, (uint32_t)wr_spi_transfer(spi, out, in, 2)), in[0]), in[1]);
        hash = mix(hash, now());
    }
    hash = mix(hash, (uint32_t)wr_display_get(display, WR_DISPLAY_BRIGHTNESS));
    hash = mix(hash, (uint32_t)wr_pulse_get(ir, WR_PULSE_RESOLUTION));
    return hash;
}
