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
    FILE* x = fopen("arduino_neopixel.wasm", "rb");
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

static OsStatus gpio_configure(void* context, uint32_t pin, uint32_t flags)
{
    (void)context;
    (void)pin;
    (void)flags;
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

/* A remote-control peripheral that keeps every train it sends, in order. */
typedef struct
{
    uint32_t pin;
    uint8_t bytes[16];
    uint32_t length;
    uint32_t timing[4];
    uint32_t latch;
} Frame;

static Frame g_frames[8];
static uint32_t g_frame_count;
static uint32_t g_configured[32];
static uint32_t g_released[32];
static uint32_t g_stray;

static OsStatus rmt_configure(void* context, uint32_t pin, uint32_t flags)
{
    (void)context;
    g_configured[pin] = flags | 0x100U;
    return OS_STATUS_OK;
}

/* Only the latch after the bytes is expected: one low item. */
static OsStatus rmt_send(void* context, uint32_t pin, const uint32_t* items, uint32_t count, uint32_t carrier_hz)
{
    Frame* last = g_frame_count ? &g_frames[g_frame_count - 1U] : NULL;
    (void)context;
    if (!last || last->pin != pin || last->latch || count != 1U || carrier_hz != 0U)
        g_stray++;
    else
        last->latch = items[0];
    return OS_STATUS_OK;
}

static OsStatus rmt_send_bits(void* context, uint32_t pin, const uint8_t* data, uint32_t length, const uint32_t timing[4])
{
    Frame* frame;
    (void)context;
    if (g_frame_count == 8U || length > 16U)
    {
        g_stray++;
        return OS_STATUS_OK;
    }
    frame = &g_frames[g_frame_count++];
    frame->pin = pin;
    memcpy(frame->bytes, data, length);
    frame->length = length;
    memcpy(frame->timing, timing, sizeof(frame->timing));
    frame->latch = 0U;
    return OS_STATUS_OK;
}

static void rmt_finish(void* context, uint32_t pin)
{
    (void)context;
    (void)pin;
}

static void rmt_release(void* context, uint32_t pin)
{
    (void)context;
    g_released[pin]++;
}

static char g_output[1024];
static size_t g_output_length;

static void capture(void* context, uint32_t task_id, uint32_t fd, const void* data, uint32_t size)
{
    (void)context;
    (void)task_id;
    (void)fd;
    if (g_output_length + size < sizeof(g_output))
    {
        memcpy(g_output + g_output_length, data, size);
        g_output_length += size;
    }
}

static int frame_is(uint32_t index, uint32_t pin, const uint8_t* bytes, uint32_t length, const uint32_t timing[4])
{
    const Frame* frame = &g_frames[index];
    return index < g_frame_count && frame->pin == pin && frame->length == length &&
           memcmp(frame->bytes, bytes, length) == 0 && memcmp(frame->timing, timing, sizeof(frame->timing)) == 0 &&
           frame->latch == 300000U;
}

int main(void)
{
    static const char expected[] = "FB0000\r\nFF000000\r\n3\r\n51\r\n18\r\n1\r\nFF0000\r\nFF00\r\n80\r\nFFFFFF\r\n"
                                   "42\r\n2AFF0700\r\n255\r\n1\r\n";
    static const uint32_t fast[4] = { 400U, 850U, 800U, 450U };
    static const uint32_t slow[4] = { 500U, 2000U, 1200U, 1300U };
    static const uint8_t primaries[9] = { 0x00, 0xFF, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF };
    static const uint8_t dimmed[9] = { 0x00, 0x33, 0x00, 0x33, 0x00, 0x00, 0x00, 0x00, 0x33 };
    static const uint8_t filled[9] = { 0x00, 0x33, 0x00, 0x04, 0x02, 0x06, 0x04, 0x02, 0x06 };
    static const uint8_t rgbw[8] = { 0x01, 0x02, 0x03, 0x04, 0x00, 0x00, 0x00, 0xFF };
    Bin b = {0};
    OsGpioPort gpio = { NULL, gpio_configure, gpio_write, gpio_read };
    OsPulsePort rmt = { NULL, rmt_configure, rmt_send, rmt_send_bits, NULL, rmt_finish, rmt_release, 25U };
    OsOutputPort output = { NULL, capture };
    OsTaskHandle t = NULL;
    uint32_t i;

    l = fopen("arduino_neopixel.log", "w");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load arduino_neopixel.wasm");
    if (!b.d)
        return 1;
    ok(os_gpio_port_set(&gpio) == OS_STATUS_OK && os_pulse_port_set(&rmt) == OS_STATUS_OK &&
           os_gpio_add_pin("gpio18", 18U, OS_GPIO_CAN_OUTPUT | OS_GPIO_CAN_PULSE, 0U) == OS_STATUS_OK &&
           os_gpio_add_pin("gpio19", 19U, OS_GPIO_CAN_OUTPUT | OS_GPIO_CAN_PULSE, 0U) == OS_STATUS_OK &&
           os_gpio_add_pin("gpio20", 20U, OS_GPIO_CAN_OUTPUT, 0U) == OS_STATUS_OK &&
           os_output_port_set(&output) == OS_STATUS_OK,
       "a board with LED strips on pins 18 and 19 (pulse hardware) and a plain pin 20");
    ok(os_task_create(&t, b.d, b.n, "_start", "sketch", 65536U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK && t,
       "start the sketch");
    for (i = 0; t && i < 100000U && os_task_get_state(t) != OS_TASK_DEAD; i++)
        if (os_schedule() == OS_STATUS_NO_READY_TASKS)
            os_tick(1U);
    ok(t && os_task_get_state(t) == OS_TASK_DEAD && os_task_get_exit_code(t) == 0U, "the sketch ran and exited with 0");
    ok(g_configured[18] == 0x100U && g_configured[19] == 0x100U && g_configured[20] == 0U,
       "begin() opened pins 18 and 19 for sending, idle low; pin 20 has no pulse hardware");
    ok(g_frame_count == 4U && g_stray == 0U, "four trains, each the bytes then one latch, nothing else");
    ok(frame_is(0U, 18U, primaries, 9U, fast),
       "show() sent red, green, blue in GRB order, 3 bytes per LED at 800 kHz, latched 300 us low");
    ok(frame_is(1U, 18U, dimmed, 9U, fast), "setBrightness(51) rescaled the stored colors");
    ok(frame_is(2U, 18U, filled, 9U, fast), "fill() from pixel 1 to the end, at that brightness");
    ok(frame_is(3U, 19U, rgbw, 8U, slow), "the RGBW strip: 4 bytes per LED at 400 kHz");
    g_output[g_output_length] = '\0';
    ok(strcmp(g_output, expected) == 0,
       "getPixelColor undid the brightness; Color, ColorHSV, gamma8, gamma32, sine8, str2order");
    if (strcmp(g_output, expected) != 0)
        printf("got:\n%s\nexpected:\n%s\n", g_output, expected);
    ok(g_released[18] == 1U && g_released[19] == 1U, "the pins went back to the kernel at exit");

    if (t)
        os_task_delete(t);
    free(b.d);
    ok(os_get_task_count() == 0U, "clean OS state");
    os_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
