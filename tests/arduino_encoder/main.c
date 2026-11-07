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
    FILE* x = fopen("arduino_encoder.wasm", "rb");
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

/* The knob's encoder in memory, counted by "hardware"; one detent is 4 steps. */
static int32_t g_count;
static uint32_t g_mode = 0xFFFFFFFFU;
static uint8_t g_running;

static OsStatus fake_start(void* context, uint32_t unit, uint32_t mode, uint32_t filter_ns, uint32_t* out_filter_ns)
{
    (void)context;
    (void)unit;
    g_mode = mode;
    g_running = 1U;
    g_count = 0;
    *out_filter_ns = filter_ns;
    return OS_STATUS_OK;
}

static OsStatus fake_read(void* context, uint32_t unit, int32_t* out_count)
{
    (void)context;
    (void)unit;
    *out_count = g_count;
    return OS_STATUS_OK;
}

static OsStatus fake_write(void* context, uint32_t unit, int32_t count)
{
    (void)context;
    (void)unit;
    g_count = count;
    return OS_STATUS_OK;
}

static void fake_stop(void* context, uint32_t unit)
{
    (void)context;
    (void)unit;
    g_running = 0U;
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

int main(void)
{
    static const char expected[] =
        "position 0\r\nposition 4\r\nposition 8\r\n100\r\nposition 104\r\nposition 108\r\nreset 108\r\n0\r\n";
    static const OsCounterInfo encoder = { OS_COUNTER_QUADRATURE, 0U };
    Bin b = {0};
    OsCounterPort port = { NULL, fake_start, fake_read, fake_write, NULL, fake_stop };
    OsOutputPort output = { NULL, capture };
    OsTaskHandle t = NULL;
    uint32_t i;

    l = fopen("arduino_encoder.log", "w");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load arduino_encoder.wasm");
    if (!b.d)
        return 1;
    ok(os_counter_port_set(&port) == OS_STATUS_OK && os_counter_add("gpio18", 0U, &encoder) == OS_STATUS_OK &&
           os_output_port_set(&output) == OS_STATUS_OK,
       "a board with a quadrature encoder on pins 18 and 19");
    ok(os_task_create(&t, b.d, b.n, "_start", "sketch", 65536U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK && t,
       "start the sketch");
    for (i = 0; t && i < 100000U && os_task_get_state(t) != OS_TASK_DEAD; i++)
    {
        if (os_schedule() == OS_STATUS_NO_READY_TASKS)
        {
            /* The sketch is in delay(): the knob turns one detent. */
            g_count += 4;
            os_tick(10U);
        }
    }
    ok(t && os_task_get_state(t) == OS_TASK_DEAD && os_task_get_exit_code(t) == 0U, "the sketch ran and exited with 0");
    g_output[g_output_length] = '\0';
    ok(strcmp(g_output, expected) == 0, "Encoder read the turns, write moved the position, readAndReset lost no step");
    if (strcmp(g_output, expected) != 0)
        printf("got:\n%s\nexpected:\n%s\n", g_output, expected);
    ok(g_mode == OS_COUNTER_X4 && !g_running && os_counter_get_owner(0U) == 0U,
       "Encoder(18, 19) counted every edge (x4) of counter \"gpio18\", stopped at exit");

    if (t)
        os_task_delete(t);
    free(b.d);
    ok(os_get_task_count() == 0U, "clean OS state");
    os_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
