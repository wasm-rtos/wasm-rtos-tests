#include <wasm-rtos.h>

/* Memory the program fills before it is swapped out and checks afterwards. */
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

/* Sleeps between filling memory and reading it back; 1 when it survived. */
uint32_t app_main_sleeper(void)
{
    uint32_t before = fill(3U);
    wr_sleep_ms(100U);
    return check() == before ? 1U : 0U;
}

/* Waits for an item of the host's queue "inbox" and returns it. */
int32_t app_main_receiver(void)
{
    uint32_t item = 0U;
    uint32_t before = fill(5U);
    int32_t queue = wr_queue_open("inbox", sizeof(item), 1U);
    int32_t status = queue < 0 ? queue : wr_queue_receive(queue, &item, WR_WAIT_FOREVER);
    if (status != 0)
        return status;
    return check() == before ? (int32_t)item : -1000;
}

/* Long enough to be swapped out between many slices. */
uint32_t app_main_worker(void)
{
    uint32_t value = fill(7U);
    uint32_t i;
    for (i = 0U; i < 20000U; ++i)
        value = value * 1103515245U + 12345U + table[(i * 7U) % (sizeof(table) / sizeof(table[0]))];
    return value;
}
