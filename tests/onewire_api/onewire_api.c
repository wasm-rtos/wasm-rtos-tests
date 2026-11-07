#include <wasm-rtos.h>

#define BUS "ow0"

/* The sensors the test puts on the bus: family 0x28, serial 1, 2 and 3. */
#define ROM_A 0x2900000000000128ull
#define ROM_B 0x7000000000000228ull
/* A valid code that nothing on the bus has. */
#define ROM_ABSENT 0xF500000000000528ull

int32_t app_main_try(void)
{
    return wr_onewire_open_cstr(BUS, WR_ONEWIRE_WHOLE_BUS);
}

/* Every ROM on the bus: count << 16 | the first family << 8 | the last serial byte. */
int32_t app_main_scan(void)
{
    wr_onewire_search_state state = { 0, 0, 0 };
    int32_t count = 0;
    int32_t first = 0;
    int32_t last = 0;
    int32_t found;

    while ((found = wr_onewire_search_cstr(BUS, &state, 0)) == 1)
    {
        uint8_t rom[8];
        for (int i = 0; i < 8; ++i)
            rom[i] = (uint8_t)(state.rom >> (i * 8));
        if (wr_onewire_crc8(rom, 7u) != rom[7])
            return -1000;
        if (count++ == 0)
            first = rom[0];
        last = rom[1];
        if (count > 10)
            return -2000;
    }
    if (found < 0)
        return found;
    /* After the last one the state starts over. */
    if (state.rom != 0u || state.discrepancy != 0u || state.done != 0u)
        return -3000;
    return (count << 16) | (first << 8) | last;
}

/* A search that starts at a family finds it first. */
int32_t app_main_family(void)
{
    wr_onewire_search_state state = { WR_DS18B20_FAMILY, 64u, 0 };
    int32_t found = wr_onewire_search_cstr(BUS, &state, 0);

    if (found != 1)
        return found < 0 ? found : -1;
    return (int32_t)(state.rom & 0xFFFFu);
}

/* Only the sensor in alarm answers an alarm search: count << 8 | its serial byte. */
int32_t app_main_alarm(void)
{
    wr_onewire_search_state state = { 0, 0, 0 };
    int32_t count = 0;
    int32_t serial = 0;
    int32_t found;

    while ((found = wr_onewire_search_cstr(BUS, &state, WR_ONEWIRE_ALARM)) == 1)
    {
        serial = (int32_t)((state.rom >> 8) & 0xFFu);
        if (++count > 10)
            return -1;
    }
    return found < 0 ? found : (count << 8) | serial;
}

/* Every sensor found converts and reads: the sum of their temperatures in thousandths of a degree. */
int32_t app_main_temps(void)
{
    wr_onewire_search_state state = { 0, 0, 0 };
    int32_t devices[8];
    int32_t count = 0;
    int32_t sum = 0;
    int32_t found;

    while ((found = wr_onewire_search_cstr(BUS, &state, 0)) == 1)
    {
        if ((state.rom & 0xFFu) != WR_DS18B20_FAMILY)
            continue;
        if (count == 8)
            return -1;
        devices[count] = wr_onewire_open_cstr(BUS, state.rom);
        if (devices[count] <= 0)
            return -100 + devices[count];
        count++;
    }
    if (found < 0)
        return -200 + found;
    if (count != 3)
        return -300 - count;
    if (wr_onewire_open_cstr(BUS, ROM_A) != -WR_EINUSE || wr_onewire_open_cstr(BUS, WR_ONEWIRE_WHOLE_BUS) != -WR_EINUSE)
        return -400;
    if (wr_wait_any(&devices[0], 1u, 0u) != -WR_EBADH)
        return -500;
    /* A device without ops: only the reset and match ROM. */
    if (wr_onewire_transaction(devices[0], 0, 0u) != 0)
        return -600;
    for (int32_t i = 0; i < count; ++i)
    {
        int32_t t = 0;
        int32_t result = wr_ds18b20_convert(devices[i]);
        if (result != 0)
            return -700 + result;
        result = wr_ds18b20_read(devices[i], &t);
        if (result != 0)
            return -800 + result;
        sum += t;
    }
    return sum;
}

/* One sensor, converted while the port holds the line high in the background. */
int32_t app_main_convert_one(void)
{
    int32_t t = 0;
    int32_t sensor = wr_onewire_open_cstr(BUS, ROM_A);
    int32_t result;

    if (sensor <= 0)
        return -1;
    result = wr_ds18b20_convert(sensor);
    if (result != 0)
        return result;
    result = wr_ds18b20_read(sensor, &t);
    return result != 0 ? result : t;
}

/*
 * The whole bus, byte by byte as Arduino libraries work: convert every
 * sensor at once with the pull-up left on, then read sensor A through
 * single time slots. Leaves the pull-up on at the end for the kernel to end.
 */
int32_t app_main_hold(void)
{
    static const uint8_t skip_convert[2] = { 0xCC, 0x44 };
    static const uint8_t read = 0xBE;
    uint8_t match[9] = { 0x55 };
    uint8_t raw[2] = { 0, 0 };
    wr_onewire_search_state state = { 0, 0, 0 };
    wr_onewire_op reset = { WR_ONEWIRE_RESET, 0, 0u };
    wr_onewire_op convert[2] = { { WR_ONEWIRE_WRITE, (void*)skip_convert, 2u }, { WR_ONEWIRE_POWER, 0, 0u } };
    wr_onewire_op scratch[2] = { { WR_ONEWIRE_WRITE, (void*)&read, 1u }, { WR_ONEWIRE_READ_BITS, raw, 16u } };
    wr_onewire_op select = { WR_ONEWIRE_WRITE, match, 9u };
    int32_t bus = wr_onewire_open_cstr(BUS, WR_ONEWIRE_WHOLE_BUS);
    int32_t count = 0;

    if (bus <= 0)
        return -1;
    for (int i = 0; i < 8; ++i)
        match[1 + i] = (uint8_t)(ROM_A >> (i * 8));
    if (wr_onewire_transaction(bus, &reset, 1u) != 0 || wr_onewire_transaction(bus, convert, 2u) != 0)
        return -2;
    /* The test looks at the line and tries the bus from elsewhere meanwhile. */
    wr_sleep_ms(20u);
    /* The program holding the bus may search it itself. */
    while (wr_onewire_search_cstr(BUS, &state, 0) == 1)
        count++;
    if (count != 4)
        return -3;
    if (wr_onewire_transaction(bus, &reset, 1u) != 0 || wr_onewire_transaction(bus, &select, 1u) != 0 ||
        wr_onewire_transaction(bus, scratch, 2u) != 0)
        return -4;
    if (wr_onewire_transaction(bus, &reset, 1u) != 0 || wr_onewire_transaction(bus, convert, 2u) != 0)
        return -5;
    return (int32_t)(int16_t)(raw[0] | (raw[1] << 8)) * 125 / 2;
}

int32_t app_main_search_busy(void)
{
    wr_onewire_search_state state = { 0, 0, 0 };
    int32_t search = wr_onewire_search_cstr(BUS, &state, 0);
    int32_t open = wr_onewire_open_cstr(BUS, ROM_A);
    return search == -WR_EINUSE && open == -WR_EINUSE ? 0 : -1;
}

/* No device answers: a search finds nothing, a device gets no presence pulse. */
int32_t app_main_empty(void)
{
    wr_onewire_search_state state = { 0, 0, 0 };
    int32_t found = wr_onewire_search_cstr(BUS, &state, 0);
    int32_t sensor = wr_onewire_open_cstr(BUS, ROM_A);

    if (found != 0 || sensor <= 0)
        return -1;
    return wr_onewire_transaction(sensor, 0, 0u);
}

int32_t app_main_errors(void)
{
    static const uint8_t byte = 0x44;
    wr_onewire_search_state state = { 0, 0, 0 };
    wr_onewire_op ops[17];
    int32_t t = 0;
    int32_t sensor = wr_onewire_open_cstr(BUS, ROM_A);
    int32_t absent = wr_onewire_open_cstr(BUS, ROM_ABSENT);

    if (sensor <= 0 || absent <= 0)
        return 1;
    if (wr_onewire_open_cstr("nope", ROM_B) != -WR_ENOTFOUND || wr_onewire_open(BUS, 3u, ROM_B, 1u) != -WR_EINVAL ||
        wr_onewire_open_cstr(BUS, ROM_B ^ 0x100u) != -WR_EINVAL)
        return 2;
    /* A device's transaction has the kernel's reset; a pull-up follows a write. */
    ops[0] = (wr_onewire_op){ WR_ONEWIRE_RESET, 0, 0u };
    if (wr_onewire_transaction(sensor, ops, 1u) != -WR_EINVAL)
        return 3;
    ops[0] = (wr_onewire_op){ WR_ONEWIRE_POWER, 0, 10u };
    if (wr_onewire_transaction(sensor, ops, 1u) != -WR_EINVAL)
        return 4;
    ops[0] = (wr_onewire_op){ WR_ONEWIRE_WRITE, (void*)&byte, 1u };
    ops[1] = (wr_onewire_op){ WR_ONEWIRE_POWER, 0, 0u };
    ops[2] = (wr_onewire_op){ WR_ONEWIRE_READ, &t, 1u };
    if (wr_onewire_transaction(sensor, ops, 3u) != -WR_EINVAL)
        return 5;
    ops[1] = (wr_onewire_op){ WR_ONEWIRE_POWER, 0, 70000u };
    if (wr_onewire_transaction(sensor, ops, 2u) != -WR_EINVAL)
        return 6;
    ops[0] = (wr_onewire_op){ 7u, 0, 0u };
    if (wr_onewire_transaction(sensor, ops, 1u) != -WR_EINVAL)
        return 7;
    ops[0] = (wr_onewire_op){ 0u, 0, 0u };
    if (wr_onewire_transaction(sensor, ops, 1u) != -WR_EINVAL)
        return 8;
    for (int i = 0; i < 17; ++i)
        ops[i] = (wr_onewire_op){ WR_ONEWIRE_WRITE, (void*)&byte, 1u };
    if (wr_onewire_transaction(sensor, ops, 17u) != -WR_ELIMIT)
        return 9;
    ops[0] = (wr_onewire_op){ WR_ONEWIRE_READ, (void*)0xFFFFFFF0u, 32u };
    if (wr_onewire_transaction(sensor, ops, 1u) != -WR_EFAULT)
        return 10;
    ops[0] = (wr_onewire_op){ WR_ONEWIRE_READ_BITS, (void*)0xFFFFFFF0u, 256u };
    if (wr_onewire_transaction(sensor, ops, 1u) != -WR_EFAULT)
        return 11;
    if (wr_onewire_transaction(sensor, (const wr_onewire_op*)0xFFFFFFF0u, 2u) != -WR_EFAULT)
        return 12;
    if (wr_onewire_transaction(-5, ops, 1u) != -WR_EBADH)
        return 13;
    /* Searches. */
    if (wr_onewire_search_cstr(BUS, &state, 2u) != -WR_EINVAL)
        return 14;
    state.discrepancy = 65u;
    if (wr_onewire_search_cstr(BUS, &state, 0) != -WR_EINVAL)
        return 15;
    if (wr_onewire_search_cstr(BUS, (wr_onewire_search_state*)0xFFFFFFF8u, 0) != -WR_EFAULT ||
        wr_onewire_search_cstr("nope", &state, 0) != -WR_ENOTFOUND)
        return 16;
    /* Nobody answers to an absent code: the scratchpad reads as ones and fails its CRC. */
    if (wr_ds18b20_read(absent, &t) != -WR_EBUS)
        return 17;
    return 0;
}
