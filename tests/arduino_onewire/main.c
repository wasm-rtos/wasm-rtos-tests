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
    FILE* x = fopen("arduino_onewire.wasm", "rb");
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
 * A 1-Wire bus in memory, slot by slot: every device sees every time slot
 * and pulls the line low or leaves it, the line is the AND of them, as on
 * the wire. Three DS18B20 sensors (family 0x28, serials 1-3; 2 is in alarm
 * and parasite-powered: it converts only with the strong pull-up), a
 * DS18S20 (family 0x10) that only answers ROM commands.
 */
enum
{
    MODE_OFF,      /* deselected until the next reset */
    MODE_ROM,      /* receiving a ROM command */
    MODE_MATCH,    /* comparing a match ROM code */
    MODE_SEARCH,   /* a search: bit, complement, the master's choice */
    MODE_FUNCTION, /* receiving a function command */
    MODE_SEND,     /* sending bits */
    MODE_QUIET     /* selected, leaving the line alone (converting, or done) */
};

typedef struct
{
    uint8_t rom[8];
    int16_t temperature; /* sixteenths of a degree */
    uint8_t alarm;
    uint8_t parasite;
    uint8_t present;
    uint8_t scratchpad[9];
    uint8_t mode;
    uint32_t bit;
    uint32_t phase;
    uint8_t shift;
    const uint8_t* out;
    uint32_t out_bits;
    uint8_t converting;
} Device;

static Device g_devices[4];
static uint8_t g_power_on;

static void device_init(Device* d, uint8_t family, uint8_t serial, int16_t temperature, uint8_t alarm,
                        uint8_t parasite)
{
    memset(d, 0, sizeof(*d));
    d->rom[0] = family;
    d->rom[1] = serial;
    d->rom[7] = os_onewire_crc8(d->rom, 7U);
    d->temperature = temperature;
    d->alarm = alarm;
    d->parasite = parasite;
    d->present = 1U;
    /* Power-on value: 85 C, 12 bits. */
    d->scratchpad[0] = 0x50U;
    d->scratchpad[1] = 0x05U;
    d->scratchpad[2] = 0x4BU;
    d->scratchpad[3] = 0x46U;
    d->scratchpad[4] = 0x7FU;
    d->scratchpad[5] = 0xFFU;
    d->scratchpad[6] = 0x0CU;
    d->scratchpad[7] = 0x10U;
    d->scratchpad[8] = os_onewire_crc8(d->scratchpad, 8U);
}

static uint8_t rom_bit(const Device* d, uint32_t bit)
{
    return (uint8_t)((d->rom[bit / 8U] >> (bit % 8U)) & 1U);
}

static uint8_t device_out(const Device* d)
{
    if (d->mode == MODE_SEND)
        return (uint8_t)((d->out[d->bit / 8U] >> (d->bit % 8U)) & 1U);
    if (d->mode == MODE_SEARCH && d->phase == 0U)
        return rom_bit(d, d->bit);
    if (d->mode == MODE_SEARCH && d->phase == 1U)
        return (uint8_t)!rom_bit(d, d->bit);
    return 1U;
}

static void device_send(Device* d, const uint8_t* data, uint32_t bits)
{
    d->mode = MODE_SEND;
    d->out = data;
    d->out_bits = bits;
    d->bit = 0U;
}

static void device_in(Device* d, uint8_t level)
{
    switch (d->mode)
    {
    case MODE_ROM:
    case MODE_FUNCTION:
        d->shift = (uint8_t)(d->shift | (level << d->bit));
        if (++d->bit < 8U)
            return;
        d->bit = 0U;
        if (d->mode == MODE_ROM)
        {
            if (d->shift == 0x55U)
                d->mode = MODE_MATCH;
            else if (d->shift == 0xCCU)
                d->mode = MODE_FUNCTION;
            else if (d->shift == 0xF0U || (d->shift == 0xECU && d->alarm))
                d->mode = MODE_SEARCH;
            else if (d->shift == 0x33U)
                device_send(d, d->rom, 64U);
            else
                d->mode = MODE_OFF;
        }
        else if (d->rom[0] != 0x28U)
            d->mode = MODE_OFF;
        else if (d->shift == 0x44U)
        {
            d->converting = 1U;
            d->mode = MODE_QUIET;
        }
        else if (d->shift == 0xBEU)
            device_send(d, d->scratchpad, 72U);
        else
            d->mode = MODE_OFF;
        d->shift = 0U;
        break;
    case MODE_MATCH:
        if (level != rom_bit(d, d->bit))
            d->mode = MODE_OFF;
        else if (++d->bit == 64U)
        {
            d->bit = 0U;
            d->mode = MODE_FUNCTION;
        }
        break;
    case MODE_SEARCH:
        if (d->phase < 2U)
        {
            d->phase++;
            break;
        }
        d->phase = 0U;
        if (level != rom_bit(d, d->bit))
            d->mode = MODE_OFF;
        else if (++d->bit == 64U)
        {
            d->bit = 0U;
            d->mode = MODE_FUNCTION;
        }
        break;
    case MODE_SEND:
        if (++d->bit == d->out_bits)
            d->mode = MODE_QUIET;
        break;
    default:
        break;
    }
}

/* One time slot: the master writes level (a read writes 1) and gets the line. */
static uint8_t slot(uint8_t level)
{
    uint8_t line = level;
    uint32_t i;
    for (i = 0; i < 4U; i++)
        if (g_devices[i].present && g_devices[i].mode != MODE_OFF)
            line &= device_out(&g_devices[i]);
    for (i = 0; i < 4U; i++)
        if (g_devices[i].present && g_devices[i].mode != MODE_OFF)
            device_in(&g_devices[i], line);
    return line;
}

static int bus_reset(void)
{
    int presence = 0;
    for (uint32_t i = 0; i < 4U; i++)
    {
        Device* d = &g_devices[i];
        d->mode = MODE_ROM;
        d->bit = 0U;
        d->phase = 0U;
        d->shift = 0U;
        presence |= d->present;
    }
    return presence;
}

/* Conversions end with the transaction; a parasite sensor needs the pull-up. */
static void end_conversions(uint8_t powered)
{
    for (uint32_t i = 0; i < 4U; i++)
    {
        Device* d = &g_devices[i];
        if (!d->converting)
            continue;
        d->converting = 0U;
        if (d->parasite && !powered)
            continue;
        d->scratchpad[0] = (uint8_t)d->temperature;
        d->scratchpad[1] = (uint8_t)((uint16_t)d->temperature >> 8);
        d->scratchpad[8] = os_onewire_crc8(d->scratchpad, 8U);
    }
}

static OsStatus ow_transaction(void* context, uint32_t bus, const OsOneWireOp* ops, uint32_t count)
{
    uint8_t powered = 0U;
    uint32_t i;
    (void)context;
    if (bus != 1U)
        return OS_STATUS_INVALID_ARGUMENT;
    g_power_on = 0U;
    for (i = 0; i < count; i++)
    {
        const OsOneWireOp* op = &ops[i];
        uint32_t bit;
        uint32_t bits = op->kind == OS_ONEWIRE_WRITE || op->kind == OS_ONEWIRE_READ ? op->length * 8U : op->length;
        switch (op->kind)
        {
        case OS_ONEWIRE_RESET:
            if (!bus_reset())
                return OS_STATUS_NACK;
            break;
        case OS_ONEWIRE_WRITE:
        case OS_ONEWIRE_WRITE_BITS:
            for (bit = 0; bit < bits; bit++)
                slot((uint8_t)((op->data[bit / 8U] >> (bit % 8U)) & 1U));
            break;
        case OS_ONEWIRE_READ:
        case OS_ONEWIRE_READ_BITS:
            memset(op->data, 0, (bits + 7U) / 8U);
            for (bit = 0; bit < bits; bit++)
                op->data[bit / 8U] = (uint8_t)(op->data[bit / 8U] | (slot(1U) << (bit % 8U)));
            break;
        case OS_ONEWIRE_POWER:
            powered = 1U;
            if (op->length == 0U)
                g_power_on = 1U;
            break;
        default:
            return OS_STATUS_INVALID_ARGUMENT;
        }
    }
    end_conversions(powered);
    return OS_STATUS_OK;
}

static OsStatus ow_triplet(void* context, uint32_t bus, uint8_t direction, uint8_t* out_bits)
{
    uint8_t id;
    uint8_t complement;
    (void)context;
    (void)bus;
    id = slot(1U);
    complement = slot(1U);
    if (id && complement)
    {
        *out_bits = 3U;
        return OS_STATUS_OK;
    }
    if (id != complement)
        direction = id;
    slot(direction);
    *out_bits = (uint8_t)(id | (complement << 1) | (direction << 2));
    return OS_STATUS_OK;
}

static char g_output[2048];
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
        "100700000000007E ok\r\n2802000000000070 ok\r\n2801000000000029 ok\r\n2803000000000047 ok\r\n"
        "devices 4\r\nfirst sensor 2802000000000070\r\nalarm 2802000000000070\r\n"
        "2802000000000070 -10.1250\r\n2801000000000029 21.5000\r\n2803000000000047 30.0625\r\n"
        "converted after 0 polls\r\nBB3D\r\n";
    Bin b = {0};
    OsOneWirePort port = { NULL, ow_transaction, ow_triplet, NULL };
    OsOutputPort output = { NULL, capture };
    OsTaskHandle t = NULL;
    uint32_t i;

    l = fopen("arduino_onewire.log", "w");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load arduino_onewire.wasm");
    if (!b.d)
        return 1;
    device_init(&g_devices[0], 0x28U, 1U, (int16_t)(21.5 * 16), 0U, 0U);
    device_init(&g_devices[1], 0x28U, 2U, (int16_t)(-10.125 * 16), 1U, 1U);
    device_init(&g_devices[2], 0x28U, 3U, (int16_t)(30.0625 * 16), 0U, 0U);
    device_init(&g_devices[3], 0x10U, 7U, 0, 0U, 0U);
    ok(os_onewire_port_set(&port) == OS_STATUS_OK && os_onewire_add_bus("gpio4", 1U) == OS_STATUS_OK &&
           os_output_port_set(&output) == OS_STATUS_OK,
       "a board with a 1-Wire bus on pin 4: three DS18B20 (one parasite-powered, one in alarm) and a DS18S20");

    ok(os_task_create(&t, b.d, b.n, "_start", "sketch", 65536U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK && t,
       "start the sketch");
    for (i = 0; t && i < 100000U && os_task_get_state(t) != OS_TASK_DEAD; i++)
        if (os_schedule() == OS_STATUS_NO_READY_TASKS)
            os_tick(10U);
    ok(t && os_task_get_state(t) == OS_TASK_DEAD && os_task_get_exit_reason(t) == OS_TASK_EXIT_EXPLICIT &&
           os_task_get_exit_code(t) == 0U,
       "setup and one loop run, and the sketch exits with 0");
    g_output[g_output_length] = '\0';
    ok(strcmp(g_output, expected) == 0,
       "OneWire(4) searched the bus, by family and for alarms, converted on parasite power and read every sensor");
    if (strcmp(g_output, expected) != 0)
        printf("got %zu bytes:\n%s\nexpected %zu bytes:\n%s\n", strlen(g_output), g_output, strlen(expected), expected);
    ok(!g_power_on && os_onewire_get_owner(1U, OS_ONEWIRE_WHOLE_BUS) == 0U,
       "depower() ended the pull-up, and the bus is free after exit");

    if (t)
        os_task_delete(t);
    free(b.d);
    ok(os_get_task_count() == 0U, "clean OS state");
    os_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
