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
    FILE* x = fopen("onewire_api.wasm", "rb");
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
 * DS18S20 (family 0x10) that only answers ROM commands, and a device whose
 * code is broken, on the bus only for the noise check.
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

static Device g_devices[5];
static uint32_t g_slots;
static uint8_t g_power_supported = 1U;
static uint8_t g_power_on;
static uint32_t g_power_ms;
static uint8_t g_async;
static uint8_t g_pending;
static uint32_t g_finishes;
static uint32_t g_transactions;
static uint32_t g_triplets;

static void device_init(Device* d, uint8_t family, uint8_t serial, int16_t temperature, uint8_t alarm,
                        uint8_t parasite, uint8_t broken)
{
    memset(d, 0, sizeof(*d));
    d->rom[0] = family;
    d->rom[1] = serial;
    d->rom[7] = (uint8_t)(os_onewire_crc8(d->rom, 7U) ^ broken);
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
    g_slots++;
    for (i = 0; i < 5U; i++)
        if (g_devices[i].present && g_devices[i].mode != MODE_OFF)
            line &= device_out(&g_devices[i]);
    for (i = 0; i < 5U; i++)
        if (g_devices[i].present && g_devices[i].mode != MODE_OFF)
            device_in(&g_devices[i], line);
    return line;
}

static int bus_reset(void)
{
    int presence = 0;
    for (uint32_t i = 0; i < 5U; i++)
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
    for (uint32_t i = 0; i < 5U; i++)
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

static char g_shape[96];

static OsStatus ow_transaction(void* context, uint32_t bus, const OsOneWireOp* ops, uint32_t count)
{
    uint8_t powered = 0U;
    uint8_t background = 0U;
    size_t used = 0;
    uint32_t i;
    (void)context;
    if (bus != 1U)
        return OS_STATUS_INVALID_ARGUMENT;
    g_transactions++;
    g_power_on = 0U;
    g_shape[0] = '\0';
    for (i = 0; i < count && used + 8 < sizeof(g_shape); i++)
        used += (size_t)snprintf(g_shape + used, sizeof(g_shape) - used, "%s%c%u", i ? " " : "",
                                 "?WRwrPZ"[ops[i].kind < 7U ? ops[i].kind : 0U], (unsigned)ops[i].length);
    for (i = 0; i < count; i++)
        if (ops[i].kind == OS_ONEWIRE_POWER && !g_power_supported)
            return OS_STATUS_UNSUPPORTED;
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
            g_power_ms += op->length;
            if (op->length == 0U)
                g_power_on = 1U;
            else
                background = g_async;
            break;
        default:
            return OS_STATUS_INVALID_ARGUMENT;
        }
    }
    end_conversions(powered);
    if (background)
    {
        g_pending = 1U;
        return OS_STATUS_PENDING;
    }
    return OS_STATUS_OK;
}

static OsStatus ow_triplet(void* context, uint32_t bus, uint8_t direction, uint8_t* out_bits)
{
    uint8_t id;
    uint8_t complement;
    (void)context;
    (void)bus;
    g_triplets++;
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

static void ow_finish(void* context, uint32_t bus)
{
    (void)context;
    g_finishes++;
    if (g_pending)
    {
        g_pending = 0U;
        os_bus_complete(OS_BUS_ONEWIRE, bus, OS_STATUS_OK);
    }
}

static uint32_t g_traces;
static OsBusTrace g_last_trace;
static OsOneWireOp g_trace_ops[2];
static uint8_t g_trace_select[9];

static void trace(void* context, const OsBusTrace* record)
{
    const OsOneWireOp* ops = (const OsOneWireOp*)record->ops;
    (void)context;
    g_traces++;
    g_last_trace = *record;
    if (record->count >= 2U)
    {
        memcpy(g_trace_ops, ops, sizeof(g_trace_ops));
        if (ops[1].kind == OS_ONEWIRE_WRITE && ops[1].length == 9U)
            memcpy(g_trace_select, ops[1].data, 9U);
    }
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

static void put_devices(void)
{
    device_init(&g_devices[0], 0x28U, 1U, (int16_t)(21.5 * 16), 0U, 0U, 0U);
    device_init(&g_devices[1], 0x28U, 2U, (int16_t)(-10.125 * 16), 1U, 1U, 0U);
    device_init(&g_devices[2], 0x28U, 3U, (int16_t)(30.0625 * 16), 0U, 0U, 0U);
    device_init(&g_devices[3], 0x10U, 7U, 0, 0U, 0U, 0U);
    device_init(&g_devices[4], 0x28U, 9U, 0, 0U, 0U, 0x40U);
    g_devices[4].present = 0U;
}

int main(void)
{
    Bin b = {0};
    OsOneWirePort port = { NULL, ow_transaction, ow_triplet, ow_finish };
    OsOneWirePort bits_only = { NULL, ow_transaction, NULL, ow_finish };
    OsOneWirePort empty = { NULL, NULL, NULL, NULL };
    OsTaskHandle t = NULL;
    OsTaskHandle other = NULL;
    uint32_t slots;
    uint32_t i;

    l = fopen("onewire_api.log", "w");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load onewire_api.wasm");
    if (!b.d)
        return 1;
    put_devices();
    {
        static const uint8_t rom[8] = { 0x02, 0x1C, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xA2 };
        ok(os_onewire_crc8(rom, 7U) == 0xA2U, "the 1-Wire CRC-8 matches the datasheet's example code");
    }

    ok(finish(start(&b, "app_main_try"), "app_main_try") == -13, "without a port onewire-open returns -WR_EUNSUPPORTED");
    ok(os_onewire_port_set(&empty) == OS_STATUS_INVALID_ARGUMENT, "a port needs transaction");
    ok(os_onewire_port_set(&port) == OS_STATUS_OK, "install the 1-Wire port");
    ok(finish(start(&b, "app_main_try"), "app_main_try") == -13, "without buses onewire-open returns -WR_EUNSUPPORTED");
    ok(os_onewire_add_bus("ow0", 1U) == OS_STATUS_OK, "register bus ow0");
    ok(os_onewire_add_bus("ow0", 2U) == OS_STATUS_BUSY && os_onewire_add_bus("other", 1U) == OS_STATUS_BUSY &&
           os_onewire_add_bus("", 3U) == OS_STATUS_INVALID_ARGUMENT,
       "names and numbers are registered once");
    os_bus_trace_set(trace, NULL);

    g_triplets = 0U;
    ok(finish(start(&b, "app_main_scan"), "app_main_scan") == 0x041003,
       "a search finds all four codes, in ROM order: the DS18S20 first, serial 3 last");
    ok(g_triplets == 4U * 64U, "with one port triplet per bit of every code");
    ok(finish(start(&b, "app_main_family"), "app_main_family") == 0x0228,
       "a search started at family 0x28 skips the DS18S20");
    ok(finish(start(&b, "app_main_alarm"), "app_main_alarm") == 0x0102, "an alarm search finds only sensor 2");

    g_traces = 0U;
    g_power_ms = 0U;
    ok(finish(start(&b, "app_main_temps"), "app_main_temps") == 21500 - 10125 + 30062,
       "three sensors convert and read: 21.5, -10.125 (parasite, through the pull-up) and 30.062 C");
    ok(g_power_ms == 3U * 750U, "each conversion held the line high for 750 ms");
    ok(g_last_trace.kind == OS_BUS_ONEWIRE && g_last_trace.device == 0U && g_last_trace.status == OS_STATUS_OK &&
           g_trace_ops[0].kind == OS_ONEWIRE_RESET && g_trace_select[0] == 0x55U &&
           memcmp(g_trace_select + 1, g_devices[2].rom, 8U) == 0,
       "traced transactions start with the kernel's reset and match ROM with the code");
    ok(strcmp(g_shape, "Z0 W9 W1 R9") == 0, "a scratchpad read is reset, match ROM, command, 9 bytes in one transaction");

    /* Without a strong pull-up the helper waits instead; the parasite sensor then cannot convert. */
    put_devices();
    g_power_supported = 0U;
    ok(finish(start(&b, "app_main_temps"), "app_main_temps") == 21500 + 85000 + 30062,
       "without a strong pull-up the parasite sensor keeps its power-on 85 C, the others convert");
    g_power_supported = 1U;

    /* A port without triplet: the kernel searches with single time slots. */
    ok(os_onewire_port_set(&bits_only) == OS_STATUS_OK, "install a port without triplet");
    g_triplets = 0U;
    slots = g_slots;
    ok(finish(start(&b, "app_main_scan"), "app_main_scan") == 0x041003, "the search finds the same four codes");
    ok(g_triplets == 0U && g_slots - slots > 4U * 64U * 3U, "through two read slots and a write slot per bit");
    ok(finish(start(&b, "app_main_alarm"), "app_main_alarm") == 0x0102, "and the alarm search the same sensor");
    ok(os_onewire_port_set(&port) == OS_STATUS_OK, "back to the port with triplet");

    /* The whole bus: the program's own resets and ROM commands, the pull-up left on. */
    put_devices();
    t = start(&b, "app_main_hold");
    run_until_waiting(t);
    ok(os_onewire_get_owner(1U, OS_ONEWIRE_WHOLE_BUS) == os_task_get_id(t) && g_power_on,
       "a program holds the whole bus, its pull-up left on while it sleeps");
    ok(finish(start(&b, "app_main_search_busy"), "app_main_search_busy") == 0,
       "meanwhile other programs can neither search the bus nor open a device on it");
    ok(finish(t, "app_main_hold") == 21500, "the program read sensor 1 through single time slots");
    ok(!g_power_on && os_onewire_get_owner(1U, OS_ONEWIRE_WHOLE_BUS) == 0U,
       "the pull-up it left on ended when it closed the bus");

    /* A port that holds the pull-up in the background. */
    g_async = 1U;
    t = start(&b, "app_main_convert_one");
    run_until_waiting(t);
    ok(os_task_get_state(t) == OS_TASK_WAITING && g_pending && os_bus_is_busy(OS_BUS_ONEWIRE, 1U),
       "a conversion with the pull-up runs in the background, the program waits");
    other = start(&b, "app_main_try");
    for (i = 0; i < 100U && os_task_get_state(other) != OS_TASK_DEAD; i++)
        os_schedule();
    ok(os_task_get_state(other) == OS_TASK_DEAD && g_pending, "another program runs meanwhile");
    ok(finish(other, "app_main_try") == -22, "(it found the device's bus in use for the whole bus)");
    g_pending = 0U;
    os_bus_complete(OS_BUS_ONEWIRE, 1U, OS_STATUS_OK);
    ok(finish(t, "app_main_convert_one") == 21500, "the end delivered, the program reads the temperature");

    t = start(&b, "app_main_convert_one");
    run_until_waiting(t);
    g_finishes = 0U;
    ok(os_task_delete(t) == OS_STATUS_OK && g_finishes == 1U && !g_pending,
       "deleting a program in a background conversion makes the port finish first");
    g_async = 0U;

    ok(finish(start(&b, "app_main_errors"), "app_main_errors") == 0,
       "unknown bus, bad flags and CRC, kernel's reset, misplaced pull-ups, bad kinds, too many ops, bad pointers, "
       "bad search state, absent device");

    g_devices[4].present = 1U;
    ok(finish(start(&b, "app_main_scan"), "app_main_scan") == -24, "a code that arrives broken fails the search with -WR_EBUS");
    for (i = 0; i < 5U; i++)
        g_devices[i].present = 0U;
    ok(finish(start(&b, "app_main_empty"), "app_main_empty") == -23,
       "an empty bus: the search finds nothing, a device gets -WR_ENACK");

    os_bus_trace_set(NULL, NULL);
    free(b.d);
    ok(os_get_task_count() == 0U, "clean OS state");
    os_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
