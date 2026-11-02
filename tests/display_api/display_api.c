#include <wasm-rtos.h>

static uint16_t g_block[4] = { 0xF800u, 0x07E0u, 0x001Fu, 0xFFFFu };

int32_t app_main_try(void)
{
    return wr_display_open_cstr("main");
}

/* What the program learns about the round display before drawing. */
int32_t app_main_info(void)
{
    int32_t lcd = wr_display_open_cstr("main");

    if (lcd <= 0)
        return 1;
    if (wr_display_get(lcd, WR_DISPLAY_WIDTH) != 240 || wr_display_get(lcd, WR_DISPLAY_HEIGHT) != 240)
        return 2;
    if (wr_display_get(lcd, WR_DISPLAY_SHAPE) != (int32_t)WR_DISPLAY_ROUND)
        return 3;
    if (wr_display_get(lcd, WR_DISPLAY_FORMAT) != (int32_t)WR_PIXEL_RGB565)
        return 4;
    if (wr_display_get(lcd, WR_DISPLAY_FORMATS) !=
        (int32_t)(WR_DISPLAY_FORMAT_BIT(WR_PIXEL_RGB565) | WR_DISPLAY_FORMAT_BIT(WR_PIXEL_XRGB8888)))
        return 5;
    if (wr_display_get(lcd, WR_DISPLAY_DPI) != 260 || wr_display_get(lcd, WR_DISPLAY_BRIGHTNESS) != 200)
        return 6;
    /* A property of a later version: this kernel cannot answer it. */
    if (wr_display_get(lcd, 99u) != -WR_EUNSUPPORTED)
        return 7;
    if (wr_display_open_cstr("main") != -WR_EINUSE)
        return 8;
    if (wr_display_open_cstr("nope") != -WR_ENOTFOUND || wr_display_open("main", 4u, 1u) != -WR_EINVAL)
        return 9;
    /* A display is drawn on, not waited for. */
    if (wr_wait_any(&lcd, 1u, 0u) != -WR_EBADH)
        return 10;
    return 0;
}

int32_t app_main_draw(void)
{
    int32_t lcd = wr_display_open_cstr("main");
    uint32_t xrgb = 0x00FF8000u;

    if (lcd <= 0)
        return 1;
    if (wr_display_fill(lcd, 0u, 0u, 240u, 240u, WR_PIXEL_RGB565, 0x0000u) != 0)
        return 2;
    if (wr_display_write(lcd, 10u, 20u, 2u, 2u, WR_PIXEL_RGB565, g_block, sizeof(g_block)) != 0)
        return 3;
    /* Another accepted format, converted by the port. */
    if (wr_display_write(lcd, 100u, 100u, 1u, 1u, WR_PIXEL_XRGB8888, &xrgb, 4u) != 0)
        return 4;
    if (wr_display_write(lcd, 239u, 0u, 2u, 1u, WR_PIXEL_RGB565, g_block, 4u) != -WR_EINVAL)
        return 5;
    if (wr_display_write(lcd, 0u, 0u, 2u, 2u, WR_PIXEL_RGB565, g_block, 6u) != -WR_EINVAL)
        return 6;
    if (wr_display_write(lcd, 0u, 0u, 8u, 1u, WR_PIXEL_MONO1, g_block, 1u) != -WR_EUNSUPPORTED)
        return 7;
    if (wr_display_write(lcd, 0u, 0u, 2u, 2u, WR_PIXEL_RGB565, (const void*)0xFFFFFFF0u, 8u) != -WR_EFAULT)
        return 8;
    if (wr_display_write(lcd, 0u, 0u, 0u, 5u, WR_PIXEL_RGB565, g_block, 0u) != 0)
        return 9;
    if (wr_display_fill(lcd, 0u, 0u, 1u, 1u, WR_PIXEL_RGB565, 0x10000u) != -WR_EINVAL)
        return 10;
    if (wr_display_flush(lcd) != 0)
        return 11;
    if (wr_display_set_brightness(lcd, 300u) != -WR_EINVAL || wr_display_set_brightness(lcd, 128u) != 0 ||
        wr_display_get(lcd, WR_DISPLAY_BRIGHTNESS) != 128)
        return 12;
    if (wr_close(lcd) != 0 || wr_display_flush(lcd) != -WR_EBADH)
        return 13;
    return 0;
}

/* The monochrome display: the port has no fill, so the kernel writes rows. */
int32_t app_main_mono(void)
{
    static const uint8_t bits[4] = { 0xFFu, 0xC0u, 0x80u, 0x40u };
    int32_t oled = wr_display_open_cstr("oled");

    if (oled <= 0)
        return 1;
    if (wr_display_get(oled, WR_DISPLAY_SHAPE) != (int32_t)WR_DISPLAY_RECTANGLE ||
        wr_display_get(oled, WR_DISPLAY_BRIGHTNESS) != -WR_EUNSUPPORTED ||
        wr_display_set_brightness(oled, 10u) != -WR_EUNSUPPORTED)
        return 2;
    if (wr_display_fill(oled, 0u, 0u, 128u, 64u, WR_PIXEL_MONO1, 1u) != 0 ||
        wr_display_fill(oled, 5u, 5u, 3u, 3u, WR_PIXEL_MONO1, 0u) != 0)
        return 3;
    /* 10 pixels wide: two bytes a row. */
    if (wr_display_write(oled, 3u, 40u, 10u, 2u, WR_PIXEL_MONO1, bits, 4u) != 0)
        return 4;
    if (wr_display_write(oled, 3u, 40u, 10u, 2u, WR_PIXEL_MONO1, bits, 3u) != -WR_EINVAL)
        return 5;
    if (wr_display_fill(oled, 0u, 0u, 1u, 1u, WR_PIXEL_MONO1, 2u) != -WR_EINVAL)
        return 6;
    if (wr_display_flush(oled) != 0)
        return 7;
    /* Ends with the display open: the kernel gives it back. */
    return 0;
}
