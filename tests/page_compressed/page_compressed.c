#include <wasm-rtos.h>

/* 192 KB, through 32 KB of RAM: small numbers in all but the last 8 frames, random words in those. */
static uint32_t g_words[49152];

int app_main(void)
{
    int32_t timer = wr_timer_create(1u, WR_TIMER_PERIODIC);
    uint32_t sum = 0, seed = 1u;

    wr_timer_start(timer);
    for (uint32_t tick = 0; tick < 12u; tick++)
    {
        wr_wait_any(&timer, 1u, WR_WAIT_FOREVER);
        wr_timer_read(timer);
        for (uint32_t i = 0; i < 40960u; i += 7u)
            g_words[i] += (i ^ tick) & 0xFFu;
        for (uint32_t i = 40960u; i < 49152u; i++)
        {
            seed = seed * 1103515245u + 12345u;
            g_words[i] ^= seed;
        }
    }
    for (uint32_t i = 0; i < 49152u; i++)
        sum += g_words[i];
    return (int)(sum & 0x7fffffff);
}
