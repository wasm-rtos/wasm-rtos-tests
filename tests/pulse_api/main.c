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
    FILE* x = fopen("pulse_api.wasm", "rb");
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

/*
 * A remote-control peripheral with DMA: trains longer than two items and
 * every byte stream run in the background until the test's "interrupt"
 * (complete). What it was asked to do is kept for the checks.
 */
static uint32_t g_configured[32];
static uint32_t g_released;
static uint32_t g_finishes;
static uint32_t g_sent[128];
static uint32_t g_sent_count;
static uint32_t g_carrier;
static uint8_t g_bits[16];
static uint32_t g_bits_length;
static uint32_t g_timing[4];
static uint32_t* g_capture;
static uint32_t g_capture_max;
static uint32_t g_idle;
static uint32_t g_timeout;
static uint32_t g_pending_pin = 0xFFFFFFFFU;

static OsStatus rmt_configure(void* context, uint32_t pin, uint32_t flags)
{
    (void)context;
    g_configured[pin] = flags | 0x100U;
    return OS_STATUS_OK;
}

static OsStatus rmt_send(void* context, uint32_t pin, const uint32_t* items, uint32_t count, uint32_t carrier_hz)
{
    (void)context;
    if (g_sent_count + count <= 128U)
    {
        memcpy(g_sent + g_sent_count, items, count * 4U);
        g_sent_count += count;
    }
    g_carrier = carrier_hz;
    if (count <= 2U)
        return OS_STATUS_OK;
    g_pending_pin = pin;
    return OS_STATUS_PENDING;
}

static OsStatus rmt_send_bits(void* context, uint32_t pin, const uint8_t* data, uint32_t length, const uint32_t timing[4])
{
    (void)context;
    memcpy(g_bits, data, length < 16U ? length : 16U);
    g_bits_length = length;
    memcpy(g_timing, timing, sizeof(g_timing));
    g_pending_pin = pin;
    return OS_STATUS_PENDING;
}

static OsStatus rmt_receive(void* context, uint32_t pin, uint32_t* items, uint32_t max, uint32_t idle_ns,
                            uint32_t timeout_ms, uint32_t* out_count)
{
    (void)context;
    (void)out_count;
    g_capture = items;
    g_capture_max = max;
    g_idle = idle_ns;
    g_timeout = timeout_ms;
    g_pending_pin = pin;
    return OS_STATUS_PENDING;
}

static void complete(OsStatus status, uint32_t count)
{
    uint32_t pin = g_pending_pin;
    g_pending_pin = 0xFFFFFFFFU;
    g_capture = NULL;
    os_pulse_complete(pin, status, count);
}

/* Ends a capture with nothing captured, as if the kernel cut it short. */
static void rmt_finish(void* context, uint32_t pin)
{
    (void)context;
    g_finishes++;
    if (g_pending_pin == pin)
        complete(g_capture != NULL ? OS_STATUS_ABORTED : OS_STATUS_OK, 0U);
}

static void rmt_release(void* context, uint32_t pin)
{
    (void)context;
    (void)pin;
    g_released++;
}

/* What an infrared receiver module captures from a NEC frame (inverted, slightly off). */
static uint32_t nec_capture(uint32_t* items, uint8_t address, uint8_t command)
{
    uint32_t code = (uint32_t)address | ((uint32_t)(uint8_t)~address << 8) | ((uint32_t)command << 16) |
                    ((uint32_t)(uint8_t)~command << 24);
    uint32_t n = 0;
    items[n++] = 9100000U;
    items[n++] = OS_PULSE_HIGH | 4420000U;
    for (uint32_t bit = 0; bit < 32U; bit++)
    {
        items[n++] = 600000U;
        items[n++] = OS_PULSE_HIGH | ((code >> bit) & 1U ? 1650000U : 530000U);
    }
    items[n++] = 580000U;
    return n;
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
            os_tick(1U);
    if (t && os_task_get_state(t) == OS_TASK_DEAD && os_task_get_return_value(t, &v) == OS_STATUS_OK)
        result = (int32_t)v.value.i32;
    printf("     %s returned %d\n", entry, (int)result);
    if (t)
        os_task_delete(t);
    return result;
}

int main(void)
{
    static const uint8_t grb[9] = { 0x00, 0xFF, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF };
    Bin b = {0};
    OsGpioPort gpio = { NULL, gpio_configure, gpio_write, gpio_read };
    OsPulsePort rmt = { NULL, rmt_configure, rmt_send, rmt_send_bits, rmt_receive, rmt_finish, rmt_release, 25U };
    OsPulsePort broken = { NULL, rmt_configure, rmt_send, NULL, NULL, NULL, rmt_release, 25U };
    OsTaskHandle t = NULL;
    uint32_t count;

    l = fopen("pulse_api.log", "w");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load pulse_api.wasm");
    if (!b.d)
        return 1;
    ok(os_gpio_port_set(&gpio) == OS_STATUS_OK &&
           os_gpio_add_pin("strip", 18U, OS_GPIO_CAN_OUTPUT | OS_GPIO_CAN_PULSE, 0U) == OS_STATUS_OK &&
           os_gpio_add_pin("ir-in", 19U, OS_GPIO_CAN_INPUT | OS_GPIO_CAN_PULSE, 0U) == OS_STATUS_OK &&
           os_gpio_add_pin("ir-out", 20U, OS_GPIO_CAN_OUTPUT | OS_GPIO_CAN_PULSE, 0U) == OS_STATUS_OK &&
           os_gpio_add_pin("led", 2U, OS_GPIO_CAN_OUTPUT, 0U) == OS_STATUS_OK,
       "pins: an LED strip, an infrared receiver and LED with pulse hardware, a plain LED");
    ok(finish(start(&b, "app_main_try"), "app_main_try") == -13, "without a port pulse-open returns -WR_EUNSUPPORTED");
    ok(os_pulse_port_set(&broken) == OS_STATUS_INVALID_ARGUMENT && os_pulse_port_set(&rmt) == OS_STATUS_OK,
       "a port needs configure, send, finish and release");

    t = start(&b, "app_main_strip");
    run_until_waiting(t);
    ok(os_task_get_state(t) == OS_TASK_WAITING && g_bits_length == 9U && memcmp(g_bits, grb, 9U) == 0,
       "the LED bytes go to the hardware in the background, 3 per LED");
    ok(g_timing[0] == 400U && g_timing[1] == 850U && g_timing[2] == 800U && g_timing[3] == 450U,
       "with the addressable LED bit timings");
    ok(g_configured[18] == 0x100U, "the strip pin is configured as an output, idle low");
    complete(OS_STATUS_OK, 0U);
    ok(finish(t, "app_main_strip") == 0, "then the latch: a short train done at once");
    ok(g_sent_count == 1U && g_sent[0] == 80000U, "the latch is 80 us low");
    ok(g_released == 1U, "the channel is released when the program ends");

    g_sent_count = 0U;
    t = start(&b, "app_main_ir_send");
    run_until_waiting(t);
    ok(g_sent_count == 67U && g_carrier == 38000U && g_sent[0] == (OS_PULSE_HIGH | 9000000U) &&
           g_sent[1] == 4500000U && g_sent[66] == (OS_PULSE_HIGH | 562500U),
       "a NEC frame: 67 items on a 38 kHz carrier");
    complete(OS_STATUS_OK, 0U);
    ok(finish(t, "app_main_ir_send") == 0, "sent");

    t = start(&b, "app_main_ir_receive");
    run_until_waiting(t);
    ok(g_capture != NULL && g_capture_max == 80U && g_idle == 10000000U && g_timeout == 1000U,
       "the receiver captures into the program's buffer, up to 10 ms of silence");
    ok(g_configured[19] == (OS_PULSE_RECEIVE | 0x100U), "the receiver pin is configured to capture");
    count = nec_capture(g_capture, 0x04, 0x08);
    complete(OS_STATUS_OK, count);
    ok(finish(t, "app_main_ir_receive") == 0x0408, "a remote's frame is captured and decoded: address 4, command 8");

    t = start(&b, "app_main_ir_receive");
    run_until_waiting(t);
    complete(OS_STATUS_TIMEOUT, 0U);
    ok(finish(t, "app_main_ir_receive") == -7, "no remote pressed: -WR_ETIMEOUT");

    t = start(&b, "app_main_ir_receive");
    run_until_waiting(t);
    count = nec_capture(g_capture, 0x04, 0x08);
    complete(OS_STATUS_OK, 200U);
    ok(finish(t, "app_main_ir_receive") == 0x0408, "a port reporting more items than the buffer holds is cut to the buffer");

    t = start(&b, "app_main_ir_receive");
    run_until_waiting(t);
    g_finishes = 0U;
    ok(os_task_delete(t) == OS_STATUS_OK && g_finishes == 1U && g_capture == NULL,
       "deleting a program that captures makes the port stop before its memory goes");

    ok(finish(start(&b, "app_main_errors"), "app_main_errors") == 0,
       "pins without pulse hardware, unknown names, bad flags, ownership, direction, alignment, bad pointers, bad timing");

    free(b.d);
    ok(os_get_task_count() == 0U, "clean OS state");
    os_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
