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
    FILE* x = fopen("counter_api.wasm", "rb");
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

/*
 * Hardware counters in memory: unit 0 an encoder ("enc0"), unit 1 a flow
 * meter's pulse counter ("flow"), unit 2 another pulse counter ("enc1" is a
 * pulse counter despite its name, for the mode check). The test moves the
 * counts; with watch points the port reports leaving the window as its
 * interrupt would.
 */
static int32_t g_count[3];
static uint32_t g_mode[3];
static uint8_t g_running[3];
static uint32_t g_stops;
static int32_t g_low[3] = { INT32_MIN, INT32_MIN, INT32_MIN };
static int32_t g_high[3] = { INT32_MAX, INT32_MAX, INT32_MAX };
static uint32_t g_watches;

static OsStatus fake_start(void* context, uint32_t unit, uint32_t mode, uint32_t filter_ns, uint32_t* out_filter_ns)
{
    (void)context;
    if (unit > 2U)
        return OS_STATUS_INVALID_ARGUMENT;
    g_mode[unit] = mode;
    g_running[unit] = 1U;
    g_count[unit] = 0;
    /* The filter counts in 12.5 ns steps up to 100 of them, like a 80 MHz clock. */
    *out_filter_ns = filter_ns == 0U ? 0U : ((filter_ns * 2U + 24U) / 25U * 25U + 1U) / 2U;
    if (*out_filter_ns > 1250U)
        *out_filter_ns = 1250U;
    return OS_STATUS_OK;
}

static OsStatus fake_read(void* context, uint32_t unit, int32_t* out_count)
{
    (void)context;
    *out_count = g_count[unit];
    return OS_STATUS_OK;
}

static OsStatus fake_write(void* context, uint32_t unit, int32_t count)
{
    (void)context;
    g_count[unit] = count;
    return OS_STATUS_OK;
}

static OsStatus fake_watch(void* context, uint32_t unit, int32_t low, int32_t high)
{
    (void)context;
    g_low[unit] = low;
    g_high[unit] = high;
    g_watches++;
    return OS_STATUS_OK;
}

static void fake_stop(void* context, uint32_t unit)
{
    (void)context;
    g_running[unit] = 0U;
    g_stops++;
}

/* The hardware moves: an interrupt when the count leaves the watched window. */
static void move(uint32_t unit, int32_t count, int with_interrupt)
{
    g_count[unit] = count;
    if (with_interrupt && (count < g_low[unit] || count > g_high[unit]))
        os_counter_event(unit);
}

static OsTaskHandle start(Bin* b, const char* entry)
{
    OsTaskHandle t = NULL;
    if (os_task_create(&t, b->d, b->n, entry, entry, 0U, OS_TASK_PRIORITY_NORMAL) != OS_STATUS_OK)
        return NULL;
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
    for (uint32_t i = 0; t && i < 100000U && os_task_get_state(t) != OS_TASK_DEAD; i++)
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
    static const OsCounterInfo encoder = { OS_COUNTER_QUADRATURE, 40000000U };
    static const OsCounterInfo pulses = { OS_COUNTER_PULSE, 1000000U };
    static const OsCounterInfo bad = { 3U, 0U };
    Bin b = {0};
    OsCounterPort port = { NULL, fake_start, fake_read, fake_write, fake_watch, fake_stop };
    OsCounterPort polled = { NULL, fake_start, fake_read, fake_write, NULL, fake_stop };
    OsCounterPort empty = { NULL, fake_start, fake_read, NULL, NULL, fake_stop };
    OsTaskHandle t = NULL;
    uint32_t started;
    uint32_t i;

    l = fopen("counter_api.log", "w");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load counter_api.wasm");
    if (!b.d)
        return 1;

    ok(finish(start(&b, "app_main_try"), "app_main_try") == -13, "without a port counter-open returns -WR_EUNSUPPORTED");
    ok(os_counter_port_set(&empty) == OS_STATUS_INVALID_ARGUMENT, "a port needs start, read, write and stop");
    ok(os_counter_port_set(&port) == OS_STATUS_OK, "install a counter port with watch points");
    ok(finish(start(&b, "app_main_try"), "app_main_try") == -13, "without counters counter-open returns -WR_EUNSUPPORTED");
    ok(os_counter_add("enc0", 0U, &encoder) == OS_STATUS_OK && os_counter_add("flow", 1U, &pulses) == OS_STATUS_OK &&
           os_counter_add("enc1", 2U, &pulses) == OS_STATUS_OK,
       "register an encoder and two pulse counters");
    ok(os_counter_add("enc0", 5U, &pulses) == OS_STATUS_BUSY && os_counter_add("other", 1U, &pulses) == OS_STATUS_BUSY &&
           os_counter_add("odd", 6U, &bad) == OS_STATUS_INVALID_ARGUMENT && os_counter_add("x", 7U, NULL) == OS_STATUS_INVALID_ARGUMENT,
       "names and units are registered once, with a known kind");

    ok(finish(start(&b, "app_main_basic"), "app_main_basic") == 0,
       "kind, speed, filter cut to what the hardware has, signed counts, homing, ownership, modes of the other kind, bad pointers and windows, "
       "a count already outside");
    ok(g_mode[0] == OS_COUNTER_X4 && !g_running[0] && g_stops == 1U, "the encoder ran in x4 and stopped when the program ended");

    t = start(&b, "app_main_wait");
    run_until_waiting(t);
    ok(os_task_get_state(t) == OS_TASK_WAITING && os_counter_get_owner(0U) == os_task_get_id(t) && g_low[0] == 0 &&
           g_high[0] == 0,
       "a program waits for the knob to move, the hardware watches around 0");
    move(0U, 0, 1);
    for (i = 0; i < 20U; i++)
        if (os_schedule() == OS_STATUS_NO_READY_TASKS)
            os_tick(1U);
    ok(os_task_get_state(t) == OS_TASK_WAITING, "no movement, no wakeup");
    move(0U, -3, 1);
    ok(finish(t, "app_main_wait") == -3, "the interrupt woke it: the knob turned back 3 steps");

    ok(finish(start(&b, "app_main_wait_timeout"), "app_main_wait_timeout") == -7, "nothing moves: -WR_ETIMEOUT");

    /* Hardware without watch points: the kernel reads the count itself. */
    ok(os_counter_port_set(&polled) == OS_STATUS_OK, "a counter port without watch points");
    t = start(&b, "app_main_flow");
    run_until_waiting(t);
    ok(os_task_get_state(t) == OS_TASK_WAITING && g_mode[1] == OS_COUNTER_RISING, "a flow meter program sleeps until 100 pulses");
    move(1U, 60, 0);
    for (i = 0; i < 30U; i++)
        if (os_schedule() == OS_STATUS_NO_READY_TASKS)
            os_tick(1U);
    ok(os_task_get_state(t) == OS_TASK_WAITING, "60 pulses: still asleep");
    move(1U, 150, 0);
    started = os_get_tick_ms();
    for (i = 0; i < 100U && os_task_get_state(t) == OS_TASK_WAITING; i++)
        if (os_schedule() == OS_STATUS_NO_READY_TASKS)
            os_tick(1U);
    ok(os_task_get_state(t) != OS_TASK_WAITING && os_get_tick_ms() - started <= OS_COUNTER_POLL_MS,
       "150 pulses: the kernel's next read woke it within the poll interval");
    ok(finish(t, "app_main_flow") == 150, "and it read the count");

    free(b.d);
    ok(os_get_task_count() == 0U, "clean OS state");
    os_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
