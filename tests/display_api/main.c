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
    FILE* x = fopen("display_api.wasm", "rb");
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

/* Two panels in memory: a round RGB565 LCD (display 0) and a 128x64 OLED (display 1). */
static uint16_t g_lcd[240 * 240];
static uint8_t g_oled[128 * 64];
static uint32_t g_writes;
static uint32_t g_fills;
static uint32_t g_flushes;
static uint8_t g_brightness;

static OsStatus fake_write(void* context, uint32_t display, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                           uint32_t format, const void* pixels, uint32_t size)
{
    const uint8_t* bytes = (const uint8_t*)pixels;
    uint32_t row;
    uint32_t column;
    (void)context;
    (void)size;
    g_writes++;
    for (row = 0; row < height; row++)
    {
        for (column = 0; column < width; column++)
        {
            if (display == 0U && format == OS_DISPLAY_RGB565)
            {
                const uint8_t* p = bytes + (row * width + column) * 2U;
                g_lcd[(y + row) * 240U + x + column] = (uint16_t)(p[0] | (p[1] << 8));
            }
            else if (display == 0U && format == OS_DISPLAY_XRGB8888)
            {
                const uint8_t* p = bytes + (row * width + column) * 4U;
                g_lcd[(y + row) * 240U + x + column] = (uint16_t)(((p[2] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[0] >> 3));
            }
            else if (display == 1U && format == OS_DISPLAY_MONO1)
            {
                uint8_t byte = bytes[row * ((width + 7U) / 8U) + column / 8U];
                g_oled[(y + row) * 128U + x + column] = (uint8_t)((byte >> (7U - column % 8U)) & 1U);
            }
            else
            {
                return OS_STATUS_INVALID_ARGUMENT;
            }
        }
    }
    return OS_STATUS_OK;
}

/* Only the LCD has a fill of its own; for the OLED the kernel writes rows. */
static OsStatus fake_fill(void* context, uint32_t display, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                          uint32_t format, uint32_t color)
{
    uint32_t row;
    uint32_t column;
    (void)context;
    if (display != 0U || format != OS_DISPLAY_RGB565)
        return OS_STATUS_UNSUPPORTED;
    g_fills++;
    for (row = 0; row < height; row++)
        for (column = 0; column < width; column++)
            g_lcd[(y + row) * 240U + x + column] = (uint16_t)color;
    return OS_STATUS_OK;
}

static OsStatus fake_flush(void* context, uint32_t display)
{
    (void)context;
    (void)display;
    g_flushes++;
    return OS_STATUS_OK;
}

static OsStatus fake_brightness(void* context, uint32_t display, uint8_t level)
{
    (void)context;
    (void)display;
    g_brightness = level;
    return OS_STATUS_OK;
}

static int32_t run_entry(Bin* b, const char* entry)
{
    OsTaskHandle t = NULL;
    OsValue v;
    uint32_t i;
    int32_t result = -100000;
    if (os_task_create(&t, b->d, b->n, entry, entry, 0U, OS_TASK_PRIORITY_NORMAL) != OS_STATUS_OK)
        return result;
    for (i = 0; i < 1000U && os_task_get_state(t) != OS_TASK_DEAD; i++)
    {
        if (os_schedule() == OS_STATUS_NO_READY_TASKS)
            os_tick(10U);
    }
    if (os_task_get_state(t) == OS_TASK_DEAD && os_task_get_return_value(t, &v) == OS_STATUS_OK)
        result = (int32_t)v.value.i32;
    if (result != 0)
        printf("     %s returned %d\n", entry, (int)result);
    os_task_delete(t);
    return result;
}

static int oled_ones(void)
{
    int count = 0;
    uint32_t index;
    for (index = 0; index < sizeof(g_oled); index++)
        count += g_oled[index];
    return count;
}

int main(void)
{
    Bin b = {0};
    OsDisplayPort port = { NULL, fake_write, fake_fill, fake_flush, fake_brightness };
    OsDisplayPort no_write = { NULL, NULL, NULL, NULL, NULL };
    OsDisplayInfo round_lcd = { 240U, 240U, OS_DISPLAY_ROUND, OS_DISPLAY_RGB565, 260U,
                                OS_DISPLAY_FORMAT_BIT(OS_DISPLAY_RGB565) | OS_DISPLAY_FORMAT_BIT(OS_DISPLAY_XRGB8888),
                                1U, 200U };
    OsDisplayInfo oled = { 128U, 64U, OS_DISPLAY_RECTANGLE, OS_DISPLAY_MONO1, 0U,
                           OS_DISPLAY_FORMAT_BIT(OS_DISPLAY_MONO1), 0U, 0U };
    OsDisplayInfo bad = round_lcd;

    l = fopen("display_api.log", "w");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load display_api.wasm");
    if (!b.d)
        return 1;

    ok(run_entry(&b, "app_main_try") == -13, "without a port display-open returns -WR_EUNSUPPORTED");

    ok(os_display_port_set(&no_write) == OS_STATUS_INVALID_ARGUMENT, "a port needs write");
    ok(os_display_port_set(&port) == OS_STATUS_OK, "install the display port");
    ok(os_display_add("main", 0U, &round_lcd) == OS_STATUS_OK, "register the round LCD");
    ok(os_display_add("oled", 1U, &oled) == OS_STATUS_OK, "register the monochrome OLED");
    ok(os_display_add("main", 2U, &oled) == OS_STATUS_BUSY && os_display_add("other", 0U, &oled) == OS_STATUS_BUSY,
       "names and numbers are registered once");
    ok(os_display_add("third", 2U, &oled) == OS_STATUS_LIMIT_REACHED, "OS_DISPLAY_MAX displays");
    bad.formats = OS_DISPLAY_FORMAT_BIT(OS_DISPLAY_XRGB8888);
    ok(os_display_add("bad", 3U, &bad) == OS_STATUS_INVALID_ARGUMENT, "the native format must be accepted");

    ok(run_entry(&b, "app_main_info") == 0, "size, shape, formats, dpi, brightness; one owner; not waitable");
    ok(os_display_get_owner(0U) == 0U, "the display is free again when the program ends");

    ok(run_entry(&b, "app_main_draw") == 0, "writes are checked, fill, flush and brightness work");
    ok(g_lcd[20 * 240 + 10] == 0xF800U && g_lcd[20 * 240 + 11] == 0x07E0U &&
           g_lcd[21 * 240 + 10] == 0x001FU && g_lcd[21 * 240 + 11] == 0xFFFFU,
       "the RGB565 block lands where it was written");
    ok(g_lcd[100 * 240 + 100] == 0xFC00U, "an XRGB8888 pixel reaches the port in its own format");
    ok(g_lcd[0] == 0U && g_fills == 1U, "the port's own fill cleared the screen");
    ok(g_flushes == 1U && g_brightness == 128U, "flush and brightness reach the port");

    g_writes = 0U;
    ok(run_entry(&b, "app_main_mono") == 0, "monochrome writes and fills; no brightness control");
    ok(oled_ones() == 128 * 64 - 9 - 8, "the kernel's fill by rows and the 1-bit block are exact");
    ok(g_oled[40 * 128 + 3] == 1U && g_oled[40 * 128 + 12] == 1U && g_oled[41 * 128 + 3] == 1U &&
           g_oled[41 * 128 + 4] == 0U && g_oled[41 * 128 + 12] == 1U,
       "1-bit rows start on a byte, leftmost pixel in the top bit");
    ok(g_writes == 64U + 3U + 1U, "without a port fill the kernel writes one run per row");
    ok(os_display_get_owner(1U) == 0U, "a display left open is given back");

    free(b.d);
    ok(os_get_task_count() == 0U, "clean OS state");
    os_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
