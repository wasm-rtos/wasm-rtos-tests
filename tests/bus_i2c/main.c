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
    FILE* x = fopen("bus_i2c.wasm", "rb");
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
 * A bus in memory: a temperature sensor at 0x48 (register pointer, 25.5 C
 * in register 0) and a display at 0x3C that takes writes. 0x51 answers its
 * address but then breaks the bus, 0x52 holds the clock; every other
 * address stays silent.
 */
static uint8_t g_sensor_pointer;
static uint8_t g_display_bytes[16];
static uint32_t g_display_length;
static uint32_t g_frequency;
static char g_shape[64];

static OsStatus fake_transaction(void* context, uint32_t bus, uint32_t address, uint32_t frequency_hz,
                                 const OsI2cOp* ops, uint32_t count)
{
    uint32_t index;
    size_t used = 0;
    (void)context;
    if (bus != 1U)
        return OS_STATUS_INVALID_ARGUMENT;
    g_frequency = frequency_hz;
    /* What went over the wire: W3 R2 ... */
    g_shape[0] = '\0';
    for (index = 0; index < count && used + 8 < sizeof(g_shape); index++)
        used += (size_t)snprintf(g_shape + used, sizeof(g_shape) - used, "%s%c%u", index ? " " : "",
                                 ops[index].kind == OS_I2C_WRITE ? 'W' : 'R', (unsigned)ops[index].length);
    if (address != 0x48U && address != 0x3CU && address != 0x51U && address != 0x52U)
        return OS_STATUS_NACK;
    if (address == 0x51U && count > 0U)
        return OS_STATUS_BUS_ERROR;
    if (address == 0x52U && count > 0U)
        return OS_STATUS_TIMEOUT;
    for (index = 0; index < count; index++)
    {
        const OsI2cOp* op = &ops[index];
        uint32_t byte;
        for (byte = 0; byte < op->length; byte++)
        {
            if (address == 0x48U && op->kind == OS_I2C_WRITE)
                g_sensor_pointer = op->data[byte];
            else if (address == 0x48U)
                op->data[byte] = g_sensor_pointer == 0U ? (byte == 0U ? 0x19U : 0x80U) : 0xFFU;
            else if (op->kind == OS_I2C_WRITE && g_display_length < sizeof(g_display_bytes))
                g_display_bytes[g_display_length++] = op->data[byte];
            else
                return OS_STATUS_NACK;
        }
    }
    return OS_STATUS_OK;
}

static uint32_t g_traces;
static OsBusTrace g_last_trace;

static void trace(void* context, const OsBusTrace* record)
{
    (void)context;
    g_traces++;
    g_last_trace = *record;
}

static OsTaskHandle start(Bin* b, const char* entry)
{
    OsTaskHandle t = NULL;
    if (os_task_create(&t, b->d, b->n, entry, entry, 0U, OS_TASK_PRIORITY_NORMAL) != OS_STATUS_OK)
        return NULL;
    return t;
}

static int32_t finish(OsTaskHandle t, const char* entry)
{
    OsValue v;
    uint32_t i;
    int32_t result = -100000;
    for (i = 0; t && i < 100000U && os_task_get_state(t) != OS_TASK_DEAD; i++)
    {
        if (os_schedule() == OS_STATUS_NO_READY_TASKS)
            os_tick(10U);
    }
    if (t && os_task_get_state(t) == OS_TASK_DEAD && os_task_get_return_value(t, &v) == OS_STATUS_OK)
        result = (int32_t)v.value.i32;
    printf("     %s returned %d\n", entry, (int)result);
    if (t)
        os_task_delete(t);
    return result;
}

int main(void)
{
    Bin b = {0};
    OsI2cPort port = { NULL, fake_transaction };
    OsI2cPort empty = { NULL, NULL };
    OsTaskHandle sensor = NULL;
    OsTaskHandle display = NULL;
    uint32_t i;

    l = fopen("bus_i2c.log", "w");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load bus_i2c.wasm");
    if (!b.d)
        return 1;

    ok(finish(start(&b, "app_main_try"), "app_main_try") == -13, "without a port i2c-open returns -WR_EUNSUPPORTED");
    ok(os_i2c_port_set(&empty) == OS_STATUS_INVALID_ARGUMENT, "a port needs transaction");
    ok(os_i2c_port_set(&port) == OS_STATUS_OK, "install the I2C port");
    ok(finish(start(&b, "app_main_try"), "app_main_try") == -13, "without buses i2c-open returns -WR_EUNSUPPORTED");
    ok(os_i2c_add_bus("i2c0", 1U, 400000U) == OS_STATUS_OK, "register bus i2c0 at 400 kHz");
    ok(os_i2c_add_bus("i2c0", 2U, 100000U) == OS_STATUS_BUSY && os_i2c_add_bus("other", 1U, 100000U) == OS_STATUS_BUSY &&
           os_i2c_add_bus("slow", 3U, 0U) == OS_STATUS_INVALID_ARGUMENT,
       "names and numbers are registered once; a bus has a speed");
    os_bus_trace_set(trace, NULL);

    /* Two programs on one bus, each with its own address. */
    sensor = start(&b, "app_main_sensor");
    display = start(&b, "app_main_display");
    for (i = 0; i < 100U && os_task_get_state(sensor) != OS_TASK_WAITING; i++)
        os_schedule();
    ok(os_i2c_get_owner(1U, 0x48U) == os_task_get_id(sensor), "the sensor's program owns address 0x48");
    ok(strcmp(g_shape, "W1 R2") == 0 && g_frequency == 400000U,
       "a register read is a write and a read in one transaction, at the bus speed");
    ok(finish(display, "app_main_display") == 0, "the display's program shares the bus, but not the address");
    ok(g_display_length == 7U && g_display_bytes[0] == 0x00U && g_display_bytes[1] == 0xAFU &&
           g_display_bytes[2] == 0x40U && g_display_bytes[6] == 4U,
       "the display got its command and its data");
    ok(finish(sensor, "app_main_sensor") == 0x1980, "the sensor read 25.5 C");
    ok(os_i2c_get_owner(1U, 0x48U) == 0U && os_i2c_get_owner(1U, 0x3CU) == 0U, "addresses are free when programs end");
    ok(g_traces == 3U && g_last_trace.kind == OS_BUS_I2C && g_last_trace.device == 0x3CU &&
           g_last_trace.frequency_hz == 400000U && g_last_trace.status == OS_STATUS_OK && g_last_trace.count == 2U,
       "the trace hook saw every transaction");

    g_traces = 0U;
    ok(finish(start(&b, "app_main_scan"), "app_main_scan") == 0x3C485152, "a scan finds the four devices that answer");
    ok(g_traces == 0x78U - 0x08U && strcmp(g_shape, "") == 0, "a probe sends only the address");

    ok(finish(start(&b, "app_main_errors"), "app_main_errors") == 0,
       "unknown bus, bad address and flags, NACK, bus error, timeout, bad pointer, too many ops, not waitable");
    ok(g_last_trace.status == OS_STATUS_TIMEOUT && g_last_trace.device == 0x52U, "failed transactions are traced too");

    os_bus_trace_set(NULL, NULL);
    free(b.d);
    ok(os_get_task_count() == 0U, "clean OS state");
    os_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
