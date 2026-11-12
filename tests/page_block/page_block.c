#include <wasm-rtos.h>

/* 64 KiB the program fills and checks: with its stack, about 2 pages of memory. */
static uint32_t table[16 * 1024];

static uint32_t fill(uint32_t seed)
{
    uint32_t sum = 0U;
    uint32_t i;
    for (i = 0U; i < sizeof(table) / sizeof(table[0]); i += 7U)
    {
        table[i] = seed * 2654435761U + i;
        sum += table[i];
    }
    return sum;
}

static uint32_t check(void)
{
    uint32_t sum = 0U;
    uint32_t i;
    for (i = 0U; i < sizeof(table) / sizeof(table[0]); i += 7U)
        sum += table[i];
    return sum;
}

/*
 * Fills its memory, sleeps while another program needs the RAM, checks it,
 * then sleeps twice more while the RAM is free again; 1 when it survived.
 */
uint32_t app_main_sleeper(void)
{
    uint32_t before = fill(3U);
    uint32_t survived;
    wr_sleep_ms(200U);
    survived = check() == before ? 1U : 0U;
    wr_sleep_ms(50U);
    wr_sleep_ms(50U);
    return survived && check() == before ? 1U : 0U;
}

/* Long work over all of its memory. */
uint32_t app_main_worker(void)
{
    uint32_t value = fill(7U);
    uint32_t i;
    for (i = 0U; i < 20000U; ++i)
        value = value * 1103515245U + 12345U + table[(i * 7U) % (sizeof(table) / sizeof(table[0]))];
    return value;
}
