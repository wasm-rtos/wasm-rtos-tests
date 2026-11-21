#include <wasm-rtos.h>

/* 100 KB the sleeper fills, sleeps on and sums; the waker 64 KB of it. */
static uint32_t g_words[25600];

static void sleep_ms(uint32_t ms)
{
    int32_t timer = wr_timer_create(ms, 0);
    wr_timer_start(timer);
    wr_wait_any(&timer, 1u, WR_WAIT_FOREVER);
}

int app_main_sleeper(void)
{
    uint32_t sum = 0;
    for (uint32_t i = 0; i < 25600u; i++)
        g_words[i] = i * 2654435761u;
    sleep_ms(3000u);
    for (uint32_t i = 0; i < 25600u; i++)
        sum += g_words[i] ^ i;
    return (int)(sum & 0x7fffffff);
}

int app_main_waker(void)
{
    uint32_t sum = 0;
    for (uint32_t i = 0; i < 16384u; i += 7u)
        g_words[i] = i;
    for (uint32_t i = 0; i < 16384u; i += 7u)
        sum += g_words[i];
    return (int)sum;
}
