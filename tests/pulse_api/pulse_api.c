#include <wasm-rtos.h>

int32_t app_main_try(void)
{
    return wr_pulse_open_cstr("strip", 0u);
}

/* Three addressable LEDs: red, green, blue. */
int32_t app_main_strip(void)
{
    static const uint8_t grb[9] = { 0x00, 0xFF, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF };
    int32_t strip = wr_pulse_open_cstr("strip", 0u);

    if (strip <= 0)
        return -1;
    if (wr_pulse_get(strip, WR_PULSE_RESOLUTION) != 25)
        return -2;
    return wr_ws2812_show(strip, grb, sizeof(grb));
}

/* An infrared remote: NEC address 0x04, command 0x08, on a 38 kHz carrier. */
int32_t app_main_ir_send(void)
{
    static uint32_t items[WR_NEC_ITEMS];
    int32_t led = wr_pulse_open_cstr("ir-out", 0u);

    if (led <= 0)
        return -1;
    wr_nec_encode(0x04, 0x08, items);
    return wr_pulse_send(led, items, WR_NEC_ITEMS, WR_NEC_CARRIER_HZ);
}

/* An infrared receiver: capture one frame and decode it. */
int32_t app_main_ir_receive(void)
{
    static uint32_t items[80];
    uint8_t address = 0;
    uint8_t command = 0;
    int32_t receiver = wr_pulse_open_cstr("ir-in", WR_PULSE_RECEIVE);
    int32_t count;

    if (receiver <= 0)
        return -1000 + receiver;
    count = wr_pulse_receive(receiver, items, 80u, 10000000u, 1000u);
    if (count < 0)
        return count;
    if (wr_nec_decode(items, (uint32_t)count, &address, &command) != 0)
        return -2000 - count;
    return (address << 8) | command;
}

int32_t app_main_errors(void)
{
    static uint32_t items[4];
    uint8_t bytes[2] = { 1, 2 };
    int32_t strip;
    int32_t receiver;

    if (wr_pulse_open_cstr("led", 0u) != -WR_EUNSUPPORTED || wr_pulse_open_cstr("nope", 0u) != -WR_ENOTFOUND)
        return 1;
    if (wr_pulse_open_cstr("ir-in", WR_PULSE_RECEIVE | WR_PULSE_IDLE_HIGH) != -WR_EINVAL ||
        wr_pulse_open_cstr("strip", 8u) != -WR_EINVAL)
        return 2;
    strip = wr_pulse_open_cstr("strip", WR_PULSE_IDLE_HIGH);
    receiver = wr_pulse_open_cstr("ir-in", WR_PULSE_RECEIVE);
    if (strip <= 0 || receiver <= 0)
        return 3;
    if (wr_gpio_open_cstr("strip", WR_GPIO_OUTPUT) != -WR_EINUSE)
        return 4;
    if (wr_pulse_send(receiver, items, 1u, 0u) != -WR_EINVAL || wr_pulse_receive(strip, items, 1u, 1000u, 10u) != -WR_EINVAL)
        return 5;
    if (wr_pulse_send(strip, (const uint32_t*)((const uint8_t*)items + 1), 1u, 0u) != -WR_EINVAL ||
        wr_pulse_send(strip, (const uint32_t*)0xFFFFFFF0u, 8u, 0u) != -WR_EFAULT)
        return 6;
    if (wr_pulse_receive(receiver, items, 0u, 1000u, 10u) != -WR_EINVAL ||
        wr_pulse_send_bits(strip, bytes, 2u, 0u, 1u, 1u, 1u) != -WR_EINVAL)
        return 7;
    if (wr_wait_any(&strip, 1u, 0u) != -WR_EBADH || wr_pulse_get(strip, 9u) != -WR_EUNSUPPORTED)
        return 8;
    return 0;
}
